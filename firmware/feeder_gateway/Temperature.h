#pragma once
#include <Arduino.h>
#include <cmath>
#include "driver/temperature_sensor.h"

// Die temperature, not ambient/box/PoE temperature. Use IDF directly because
// Arduino temperatureRead() in core 3.3.0 configures only a 10–50 C range.
temperature_sensor_handle_t chipSensor = nullptr;
float chipTemperature = NAN, chipTemperatureMin = NAN, chipTemperatureMax = NAN;
uint32_t temperatureAttemptMs = 0, temperatureSampleMs = 0;
esp_err_t temperatureError = ESP_ERR_INVALID_STATE;

void sampleTemperature() {
  temperatureAttemptMs = millis();
  float value = NAN;
  temperatureError = chipSensor ? temperature_sensor_get_celsius(chipSensor, &value) : temperatureError;
  chipTemperature = temperatureError == ESP_OK && std::isfinite(value) ? value : NAN;
  if (std::isfinite(chipTemperature)) {
    temperatureSampleMs = millis();
    if (!std::isfinite(chipTemperatureMin) || value < chipTemperatureMin) chipTemperatureMin = value;
    if (!std::isfinite(chipTemperatureMax) || value > chipTemperatureMax) chipTemperatureMax = value;
  }
}

void startTemperature() {
  temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
  temperatureError = temperature_sensor_install(&config, &chipSensor);
  if (temperatureError == ESP_OK) {
    temperatureError = temperature_sensor_enable(chipSensor);
    if (temperatureError != ESP_OK) {
      temperature_sensor_uninstall(chipSensor);
      chipSensor = nullptr;
    }
  }
  sampleTemperature();
}

void pollTemperature() {
  if (static_cast<uint32_t>(millis() - temperatureAttemptMs) >= 5000) sampleTemperature();
}

String temperatureNumber(float value) {
  return std::isfinite(value) ? String(value, 1) : String("null");
}

String temperatureJson() {
  return String(",\"chip_temperature_c\":") + temperatureNumber(chipTemperature) +
         ",\"chip_temperature_min_c\":" + temperatureNumber(chipTemperatureMin) +
         ",\"chip_temperature_max_c\":" + temperatureNumber(chipTemperatureMax) +
         ",\"chip_temperature_sample_age_ms\":" +
         (std::isfinite(chipTemperature) ? String(static_cast<uint32_t>(millis() - temperatureSampleMs)) : String("null"));
}

String temperatureDiagnostics() {
  String result = "\nESP32 chip temperature: ";
  if (std::isfinite(chipTemperature)) result += String(chipTemperature, 1) + " C";
  else result += "unavailable (" + String(esp_err_to_name(temperatureError)) + ")";
  if (std::isfinite(chipTemperatureMin)) {
    result += "\nChip min / max since boot: " + String(chipTemperatureMin, 1) + " / " + String(chipTemperatureMax, 1) + " C";
  }
  return result;
}
