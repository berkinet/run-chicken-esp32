#pragma once
#include <Arduino.h>
#include <BLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <time.h>
#include "FeederProtocol.h"
#include "LatestCommand.h"

class FeederTransport {
 public:
  void begin() {
    settingsReady=settings.begin("feeder-control",false);
    enabled=settingsReady ? settings.getBool("enabled",true) : true;
    if(xTaskCreate(task,"feeder-ble",10000,this,1,nullptr)!=pdPASS) error="worker_start_failed";
  }
  bool connectionEnabled() const { return enabled.load(); }
  bool setConnectionEnabled(bool value) {
    if(!settingsReady || !settings.putBool("enabled",value)) return false;
    portENTER_CRITICAL(&commandLock);
    enabled=value;
    if(!value) {
      disconnectRequested=true;
      commands.disconnect(); publishCommand();
      error="disconnecting";
    }
    portEXIT_CRITICAL(&commandLock);
    return true;
  }
  bool send(bool open) {
    const bool clockReady=time(nullptr)>=1700000000;
    portENTER_CRITICAL(&commandLock);
    if(!enabled.load() || disconnectRequested.load() || !ready.load() || !clockReady) { portEXIT_CRITICAL(&commandLock); return false; }
    commands.request(open);
    publishCommand();
    portEXIT_CRITICAL(&commandLock);
    return true;
  }

  bool isBusy() const { return busy.load(); }
  const char* lastError() const { return error.load(); }
  bool simulated() const { return false; }
  bool connected() const { return ready.load(); }
  const char* state() const { return ready && physical==0 ? "open" : ready && physical==1 ? "closed" : "unknown"; }
  const char* outcome() const { const char* names[]={"none","queued","sending","confirmed","failed"}; return names[result.load()]; }
  String details() const {
    uint8_t bytes[24]; size_t len;
    portENTER_CRITICAL(&rxLock); memcpy(bytes,rxBytes,24); len=rxLength; portEXIT_CRITICAL(&rxLock);
    String raw;
    for(size_t i=0;i<24 && i<len;++i) { char hex[3]; snprintf(hex,3,"%02x",bytes[i]); raw+=hex; }
    return String(",\"feeder_connection_enabled\":")+(enabled?"true":"false")+",\"ble_rx_count\":"+String(sequence.load())+",\"ble_cccd\":"+String(cccd.load())+",\"ble_rx_length\":"+String(len)+",\"ble_rx_prefix\":\""+raw+"\""+ String(",\"ble_busy\":")+(busy?"true":"false")+",\"ble_error\":\""+error.load()+"\",\"clock_synced\":"+(time(nullptr)>=1700000000?"true":"false");
  }
 private:
  Preferences settings; // Only accessed by setup/HTTP task.
  bool settingsReady=false;
  std::atomic<bool> enabled{true},disconnectRequested{false};
  portMUX_TYPE commandLock=portMUX_INITIALIZER_UNLOCKED;
  LatestCommand commands;
  void publishCommand() { busy=commands.busy(); result=commands.result; }
  void finishCommand(bool ok) {
    portENTER_CRITICAL(&commandLock);
    commands.finish(ok); publishCommand();
    portEXIT_CRITICAL(&commandLock);
  }
  BLEClient* client=nullptr;
  BLERemoteCharacteristic* writer=nullptr;
  BLERemoteCharacteristic* reader=nullptr;
  std::atomic<int> cccd{-1};
  std::atomic<bool> ready{false},busy{false};
  std::atomic<int> physical{-1},result{0};
  std::atomic<unsigned> sequence{0};
  mutable portMUX_TYPE rxLock=portMUX_INITIALIZER_UNLOCKED;
  uint8_t rxBytes[24]={};
  size_t rxLength=0;
  std::atomic<const char*> error{"starting"};
  static FeederTransport* instance;
  static void notification(BLERemoteCharacteristic*,uint8_t* data,size_t length,bool) {
    if(!instance) return;
    portENTER_CRITICAL(&instance->rxLock);
    instance->rxLength=length;
    memcpy(instance->rxBytes,data,length<24?length:24);
    portEXIT_CRITICAL(&instance->rxLock);
    // Other message types (e.g. 0x48) are not position reports.
    if(length && data[0]==0x81) instance->physical=feederFinalState(data,length);
    instance->sequence.fetch_add(1);
  }
  void readStatus() {
    if(reader && reader->canRead() && client->isConnected()) {
      String v=reader->readValue();
      if(v.length()) notification(reader,(uint8_t*)v.c_str(),v.length(),false);
    }
  }
  static void task(void* p) { static_cast<FeederTransport*>(p)->run(); }
  void run() {
    instance=this;
    BLEDevice::init("Chicken feeder gateway");
    client=BLEDevice::createClient();
    Preferences prefs; prefs.begin("feeder-ble",false);
    String pinned=prefs.getString("address","");
    BLEScan* scan=BLEDevice::getScan(); scan->setActiveScan(true);
    uint32_t lastIdleRead=0;
    for(;;) {
      // Release this application's client only; never stop the shared BLE radio.
      if(disconnectRequested.exchange(false) || !enabled.load()) {
        portENTER_CRITICAL(&commandLock);
        ready=false; commands.disconnect(); publishCommand();
        portEXIT_CRITICAL(&commandLock);
        if(client->isConnected()) client->disconnect();
        writer=nullptr; reader=nullptr; physical=-1;
        error=enabled.load()?"reconnecting":"disabled";
        vTaskDelay(pdMS_TO_TICKS(20)); continue;
      }
      if(!client->isConnected() || !ready) {
        portENTER_CRITICAL(&commandLock);
        ready=false; commands.disconnect(); publishCommand();
        portEXIT_CRITICAL(&commandLock);
        physical=-1; writer=nullptr;
        if(client->isConnected()) client->disconnect();
        error="scanning";
        BLEScanResults* found=scan->start(3,false);
        if(!enabled.load() || disconnectRequested.load()) { scan->clearResults(); continue; }
        int match=-1,count=0;
        if(found) for(int i=0;i<found->getCount();++i) {
          auto d=found->getDevice(i);
          if(pinned.length() ? d.getAddress().toString()==pinned : d.getName()=="F-80") { match=i; ++count; }
        }
        if(count!=1) { error=count>1?"multiple_feeders":"feeder_not_found"; scan->clearResults(); vTaskDelay(pdMS_TO_TICKS(7000)); continue; }
        auto device=found->getDevice(match);
        error="connecting";
        bool ok=client->connectTimeout(&device,5000);
        scan->clearResults();
        if(!enabled.load() || disconnectRequested.load()) continue;
        if(ok) {
          client->setMTU(185);
          auto ws=client->getService("00000000-cc7a-482a-984a-7f2ed5b3e58f");
          auto rs=client->getService("0000004f-cc7a-482a-984a-7f2ed5b3e58f");
          writer=ws?ws->getCharacteristic("00000000-8e22-4541-9d4c-21edae82ed19"):nullptr;
          reader=rs?rs->getCharacteristic("00000001-8e22-4541-9d4c-21edae82ed19"):nullptr;
          ok=writer && writer->canWrite() && reader && reader->canNotify();
          if(ok) {
            reader->registerForNotify(notification);
            auto descriptor=reader->getDescriptor(BLEUUID((uint16_t)0x2902));
            if(descriptor) { auto v=descriptor->readValue(); cccd=v.length()?uint8_t(v[0]):-2; }
            if(!pinned.length()) { pinned=device.getAddress().toString(); prefs.putString("address",pinned); }
            Serial.printf("Feeder BLE address: %s\n",pinned.c_str());
            if(enabled.load() && !disconnectRequested.load() && time(nullptr)>=1700000000) { uint8_t p[32]; feederPacket(p,time(nullptr),0,true); ok=writer->writeValue(p,32,true); }
          }
        }
        if(!ok) { error="connection_or_services_failed"; if(client->isConnected()) client->disconnect(); vTaskDelay(pdMS_TO_TICKS(7000)); continue; }
        if(!enabled.load() || disconnectRequested.load()) continue;
        error="none"; ready=true;
        readStatus();
      }
      bool open=false;
      portENTER_CRITICAL(&commandLock);
      bool haveCommand=enabled.load() && !disconnectRequested.load() && commands.take(open); publishCommand();
      portEXIT_CRITICAL(&commandLock);
      if(!haveCommand) {
        if(static_cast<uint32_t>(millis()-lastIdleRead)>=2000) {
          readStatus(); lastIdleRead=millis();
        }
        vTaskDelay(pdMS_TO_TICKS(20)); continue;
      }
      error="none"; physical=-1;
      const unsigned before=sequence.load();
      bool ok=enabled.load() && !disconnectRequested.load() && client->isConnected() && time(nullptr)>=1700000000;
      if(ok) { uint8_t p[32]; feederPacket(p,time(nullptr),open?1:2); ok=writer->writeValue(p,32,true); }
      if(!ok) { error="write_failed"; physical=-1; finishCommand(false); continue; }
      uint32_t start=millis(), lastRead=0;
      while(enabled.load() && !disconnectRequested.load() && client->isConnected() && millis()-start<12000) {
        if(millis()-lastRead>=500) { readStatus(); lastRead=millis(); }
        if(sequence.load()!=before && physical.load()==(open?0:1)) break;
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      ok=enabled.load() && !disconnectRequested.load() && client->isConnected() && sequence.load()!=before && physical.load()==(open?0:1);
      error=ok?"none":"confirmation_timeout";
      if(!ok) physical=-1;
      finishCommand(ok);
    }
  }
};
FeederTransport* FeederTransport::instance=nullptr;
