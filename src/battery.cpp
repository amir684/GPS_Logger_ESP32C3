#include "battery.h"

#include <Arduino.h>

#include "config.h"
#include "settings.h"

namespace {

constexpr uint32_t kSampleMs = 1000;
constexpr float kPresentV = 2.5f;

// Resting Li-ion cell voltage vs. state of charge
const float kVolts[] = {3.00f, 3.30f, 3.60f, 3.70f, 3.75f, 3.79f, 3.83f, 3.87f, 3.92f, 3.97f, 4.10f, 4.20f};
const uint8_t kPercent[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
constexpr int kPoints = sizeof(kVolts) / sizeof(kVolts[0]);

float filtered = 0;
uint32_t lastSampleMs = 0;

float readVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
  return sum / 16.0f / 1000.0f * Settings::real(S_BAT_CAL);
}

}  // namespace

void Battery::begin() {
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  filtered = readVolts();
  lastSampleMs = millis();
}

void Battery::update() {
  if (millis() - lastSampleMs < kSampleMs) return;
  lastSampleMs = millis();
  filtered += (readVolts() - filtered) * 0.2f;
}

float Battery::volts() { return filtered; }

int Battery::percent() {
  if (filtered <= kVolts[0]) return 0;
  for (int i = 1; i < kPoints; i++) {
    if (filtered < kVolts[i]) {
      float t = (filtered - kVolts[i - 1]) / (kVolts[i] - kVolts[i - 1]);
      return kPercent[i - 1] + (int)(t * (kPercent[i] - kPercent[i - 1]) + 0.5f);
    }
  }
  return 100;
}

bool Battery::present() { return filtered > kPresentV; }

bool Battery::low() { return present() && filtered < Settings::real(S_BAT_LOW); }

int Battery::runtimeMinutes() {
  if (!present()) return -1;
  return (int)((long)Settings::get(S_BAT_MAH) * percent() * 60 / 100 / Settings::get(S_BAT_LOAD));
}
