#include "autolog.h"

#include <algorithm>

#include "app.h"
#include "gps.h"
#include "settings.h"
#include "trip.h"
#include "ui.h"

namespace {

uint32_t aboveSinceMs = 0;
uint32_t belowSinceMs = 0;
bool manualHold = false;  // logging was switched off by hand while moving: don't restart it right away

double currentSpeed() { return Gps::hasFix() ? Gps::raw().speed.kmph() : 0.0; }

// The stop speed always stays below the start speed, whatever the settings say
double stopSpeed() {
  return std::min<double>(Settings::get(S_AUTO_STOP), Settings::get(S_AUTO_START) - 1);
}

void setLogging(bool on, const char *message) {
  if (Settings::set(S_LOGGING, on)) App::applySetting(S_LOGGING);  // closes the session when turning off
  Ui::toast(message);
}

}  // namespace

void AutoLog::update() {
  if (!Settings::get(S_AUTO_LOG)) {
    aboveSinceMs = belowSinceMs = 0;
    manualHold = false;
    return;
  }

  uint32_t now = millis();
  double kmh = currentSpeed();

  if (Settings::get(S_LOGGING)) {
    aboveSinceMs = 0;
    if (kmh > stopSpeed()) {
      belowSinceMs = 0;
      return;
    }
    if (!belowSinceMs) belowSinceMs = now;
    if (now - belowSinceMs >= Settings::value(S_AUTO_STOPT) * 1000UL) {
      belowSinceMs = 0;
      manualHold = false;
      setLogging(false, "Auto stop");
    }
    return;
  }

  belowSinceMs = 0;
  if (kmh <= stopSpeed()) manualHold = false;  // standing still again: the detector is armed
  if (manualHold || kmh < Settings::get(S_AUTO_START)) {
    aboveSinceMs = 0;
    return;
  }
  if (!aboveSinceMs) aboveSinceMs = now;
  if (now - aboveSinceMs >= Settings::value(S_AUTO_STARTT) * 1000UL) {
    aboveSinceMs = 0;
    if (Settings::get(S_AUTO_TRIP)) Trip::reset();
    setLogging(true, "Auto start");
  }
}

void AutoLog::noteManual(bool on) {
  aboveSinceMs = belowSinceMs = 0;
  manualHold = !on && currentSpeed() > stopSpeed();
}

bool AutoLog::armed() { return Settings::get(S_AUTO_LOG) && !Settings::get(S_LOGGING); }

uint32_t AutoLog::stopCountdownS() {
  if (!belowSinceMs) return 0;
  uint32_t total = Settings::value(S_AUTO_STOPT) * 1000UL;
  uint32_t elapsed = millis() - belowSinceMs;
  return elapsed >= total ? 0 : (total - elapsed) / 1000 + 1;
}
