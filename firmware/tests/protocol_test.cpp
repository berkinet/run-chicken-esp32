#include "../feeder_gateway/FeederProtocol.h"
#include <assert.h>
#include <string.h>
int main() {
 uint8_t p[32];
 const uint8_t open[]={0,0x31,0x94,0xbf,0x6a,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0x2e};
 const uint8_t close[]={0,0x33,0x94,0xbf,0x6a,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0x0b};
 feederPacket(p,0x6abf9431,1); assert(memcmp(p,open,32)==0);
 feederPacket(p,0x6abf9433,2); assert(memcmp(p,close,32)==0);
 uint8_t status[153]={0x81}; assert(feederFinalState(status,153)==0);
 status[17]=1; assert(feederFinalState(status,153)==1);
 status[15]=0x20; assert(feederFinalState(status,153)==-1);
 status[15]=0;status[0]=0x48;assert(feederFinalState(status,153)==-1);
 assert(feederFinalState(status,17)==-1);
}
