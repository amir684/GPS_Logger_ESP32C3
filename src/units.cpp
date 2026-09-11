#include "units.h"

#include <Arduino.h>

#include "settings.h"

namespace {

// Same order as the "tz" setting options
const char *const kTimezones[] = {
    "UTC0",
    "IST-2IDT,M3.4.4/26,M10.5.0",
    "GMT0BST,M3.5.0/1,M10.5.0",
    "CET-1CEST,M3.5.0,M10.5.0/3",
    "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "MSK-3",
    "<+04>-4",
    "IST-5:30",
    "CST-8",
    "JST-9",
    "AEST-10AEDT,M10.1.0,M4.1.0/3",
    "EST5EDT,M3.2.0,M11.1.0",
    "CST6CDT,M3.2.0,M11.1.0",
    "MST7MDT,M3.2.0,M11.1.0",
    "PST8PDT,M3.2.0,M11.1.0",
    "<-03>3",
};
constexpr int kTimezoneCount = sizeof(kTimezones) / sizeof(kTimezones[0]);

int units() { return Settings::get(S_UNITS); }

}  // namespace

double Units::speed(double kmh) {
  switch (units()) {
    case UNITS_IMPERIAL: return kmh * 0.621371;
    case UNITS_NAUTICAL: return kmh * 0.539957;
    default: return kmh;
  }
}

const char *Units::speedUnit() {
  switch (units()) {
    case UNITS_IMPERIAL: return "mph";
    case UNITS_NAUTICAL: return "kn";
    default: return "km/h";
  }
}

double Units::distance(double meters) {
  switch (units()) {
    case UNITS_IMPERIAL: return meters / 1609.344;
    case UNITS_NAUTICAL: return meters / 1852.0;
    default: return meters / 1000.0;
  }
}

const char *Units::distanceUnit() {
  switch (units()) {
    case UNITS_IMPERIAL: return "mi";
    case UNITS_NAUTICAL: return "nm";
    default: return "km";
  }
}

double Units::altitude(double meters) { return units() == UNITS_IMPERIAL ? meters * 3.28084 : meters; }

const char *Units::altitudeUnit() { return units() == UNITS_IMPERIAL ? "ft" : "m"; }

const char *Units::timezonePosix(int index) { return kTimezones[constrain(index, 0, kTimezoneCount - 1)]; }

void Units::applyTimezone() {
  setenv("TZ", timezonePosix(Settings::get(S_TZ)), 1);
  tzset();
}

void Units::formatDate(const tm &t, char *out, size_t n) {
  switch (Settings::get(S_DATE_FMT)) {
    case 1:
      snprintf(out, n, "%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
      break;
    case 2:
      snprintf(out, n, "%02d/%02d/%04d", t.tm_mon + 1, t.tm_mday, t.tm_year + 1900);
      break;
    default:
      snprintf(out, n, "%02d.%02d.%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
      break;
  }
}

void Units::formatTime(const tm &t, bool seconds, char *out, size_t n) {
  int hour = t.tm_hour;
  if (Settings::get(S_CLOCK_12H)) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  if (seconds) {
    snprintf(out, n, "%02d:%02d:%02d", hour, t.tm_min, t.tm_sec);
  } else {
    snprintf(out, n, "%02d:%02d", hour, t.tm_min);
  }
}

const char *Units::cardinal(double degrees) {
  static const char *const kNames[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                       "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int index = (int)lround(fmod(fmod(degrees, 360.0) + 360.0, 360.0) / 22.5) % 16;
  return kNames[index];
}
