// GPS Logger ESP32-C3 - https://github.com/amir684/GPS_Logger_ESP32C3
// Copyright (c) 2026 AmirY (amir684). Licensed under the Apache License 2.0, see LICENSE and NOTICE.

#include <Arduino.h>
#include <time.h>

#include "app.h"
#include "autolog.h"
#include "battery.h"
#include "config.h"
#include "console.h"
#include "gps.h"
#include "input.h"
#include "logger.h"
#include "net.h"
#include "samples.h"
#include "settings.h"
#include "trip.h"
#include "ui.h"
#include "units.h"
#include "web.h"

History<kHistorySize> histSpeed;
History<kHistorySize> histAlt;
History<kHistorySize> histBattery;

namespace {

constexpr float kLowBatteryFlushV = 3.35f;  // flush every record to survive a brown-out

uint32_t lastSampleMs = 0;
uint32_t lastBatteryGraphMs = 0;
uint32_t lastGpsTime = 0;
uint32_t lastLoggedEpoch = 0;
bool haveLastPosition = false;
double lastLat = 0;
double lastLon = 0;

uint16_t clampU16(long v) { return v < 0 ? 0 : v > 0xFFFF ? 0xFFFF : (uint16_t)v; }

LogRecord makeRecord() {
  TinyGPSPlus &g = Gps::raw();
  LogRecord r = {};
  r.epoch = (uint32_t)time(nullptr);
  if (Gps::hasFix()) {
    r.flags |= kLogFlagFix;
    r.latE7 = lround(g.location.lat() * 1e7);
    r.lonE7 = lround(g.location.lng() * 1e7);
    r.altCm = lround(g.altitude.meters() * 100);
    r.speedCms = clampU16(lround(g.speed.mps() * 100));
    r.courseCdeg = clampU16(lround(g.course.deg() * 100));
  }
  r.hdopC = g.hdop.isValid() ? clampU16(g.hdop.value()) : 0xFFFF;
  r.sats = g.satellites.value() > 255 ? 255 : g.satellites.value();
  r.batteryMv = clampU16(lround(Battery::volts() * 1000));
  return r;
}

bool passesFilters() {
  if (!Gps::hasFix()) return Settings::get(S_LOG_NOFIX);
  TinyGPSPlus &g = Gps::raw();
  int32_t minSpeed = Settings::get(S_LOG_MINSPD);
  if (minSpeed && g.speed.kmph() < minSpeed) return false;
  int32_t minDistance = Settings::get(S_LOG_MINDIST);
  if (minDistance && haveLastPosition &&
      TinyGPSPlus::distanceBetween(lastLat, lastLon, g.location.lat(), g.location.lng()) < minDistance) {
    return false;
  }
  return true;
}

void logSample() {
  uint32_t now = (uint32_t)time(nullptr);
  // A long enough pause (including time skipped by the filters) starts a new session
  uint32_t splitS = Settings::value(S_LOG_SPLIT) * 60;
  if (splitS && lastLoggedEpoch && now - lastLoggedEpoch > splitS) Logger::close();

  if (!passesFilters() || !Logger::append(makeRecord())) return;
  lastLoggedEpoch = now;
  if (Gps::hasFix()) {
    haveLastPosition = true;
    lastLat = Gps::raw().location.lat();
    lastLon = Gps::raw().location.lng();
  }
  if (Battery::present() && Battery::volts() < kLowBatteryFlushV) Logger::flush();
}

void takeSample() {
  TinyGPSPlus &g = Gps::raw();
  if (Gps::hasFix()) {
    histSpeed.push(g.speed.kmph());
    if (g.altitude.isValid()) histAlt.push(g.altitude.meters());
  }
  if (Settings::get(S_LOGGING) && Gps::timeValid()) logSample();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Settings::begin();
  Gps::setPower(true);  // before the splash screen, so the module starts searching right away
  setCpuFrequencyMhz(Settings::value(S_CPU_MHZ));
  Units::applyTimezone();

  Ui::begin();  // first, so the splash is visible while LittleFS formats on the very first boot
  Input::begin();
  Battery::begin();
  Gps::begin();
  Logger::begin();
  Logger::setStopWhenFull(Settings::get(S_LOG_FULL) == 1);
  Net::begin();

  histBattery.push(Battery::volts());
  lastBatteryGraphMs = millis();
}

void loop() {
  Gps::update();
  Battery::update();
  Console::update();

  // Time changes once per GPS epoch (typically 1 Hz)
  uint32_t gpsTime = Gps::raw().time.value();
  if (gpsTime != lastGpsTime) {
    lastGpsTime = gpsTime;
    Trip::update();
  }

  AutoLog::update();

  uint32_t now = millis();
  if (now - lastSampleMs >= Settings::value(S_LOG_INTERVAL) * 1000UL) {
    lastSampleMs = now;
    takeSample();
  }
  if (now - lastBatteryGraphMs >= kBatteryGraphPeriodS * 1000UL) {
    lastBatteryGraphMs = now;
    histBattery.push(Battery::volts());
  }

  Ui::update();
  Net::update();
  Web::update();
  Settings::loop();
  App::loop();
  delay(5);
}
