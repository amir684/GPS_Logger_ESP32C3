#include "gps.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <sys/time.h>

#include "config.h"
#include "settings.h"

namespace {

constexpr uint32_t kResyncMs = 10 * 60 * 1000UL;

TinyGPSPlus gps;
bool uartStarted = false;
bool powerOn = false;
bool clockSet = false;
uint32_t lastSyncMs = 0;

// Days since 1970-01-01 (Howard Hinnant's days_from_civil)
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + doe - 719468;
}

void syncClock() {
  if (clockSet && millis() - lastSyncMs < kResyncMs) return;
  if (!gps.date.isValid() || !gps.time.isValid() || gps.time.age() > 500) return;
  // Modules report bogus dates (2000, 2080...) before they have heard a satellite
  uint16_t year = gps.date.year();
  if (year < 2024 || year > 2099) return;

  int64_t days = daysFromCivil(year, gps.date.month(), gps.date.day());
  timeval tv;
  tv.tv_sec = days * 86400 + gps.time.hour() * 3600 + gps.time.minute() * 60 + gps.time.second();
  tv.tv_usec = gps.time.centisecond() * 10000;
  settimeofday(&tv, nullptr);
  clockSet = true;
  lastSyncMs = millis();
}

}  // namespace

void Gps::begin() {
  setPower(true);
  Serial1.setRxBufferSize(1024);
  setBaud(Settings::value(S_GPS_BAUD));
}

void Gps::setBaud(uint32_t baud) {
  if (uartStarted) Serial1.end();
  Serial1.begin(baud, SERIAL_8N1, PIN_GPS_RX, -1);
  uartStarted = true;
}

void Gps::setPower(bool on) {
  gpio_hold_dis((gpio_num_t)PIN_GPS_POWER);  // may still be held from deep sleep
  pinMode(PIN_GPS_POWER, OUTPUT);
  digitalWrite(PIN_GPS_POWER, GPS_POWER_ACTIVE_LOW ? !on : on);
  powerOn = on;
}

bool Gps::powered() { return powerOn; }

void Gps::update() {
  while (Serial1.available()) gps.encode(Serial1.read());
  syncClock();
}

TinyGPSPlus &Gps::raw() { return gps; }

bool Gps::hasFix() { return gps.location.isValid() && gps.location.age() < 3000; }

bool Gps::timeValid() { return clockSet; }
