#include "settings.h"

#include <Preferences.h>
#include <math.h>

const char *const Settings::kGroupNames[G_COUNT] = {"Logging", "Display", "Screens", "Units & time",
                                                     "GPS",     "Battery", "WiFi",    "System"};

namespace {

constexpr uint32_t kSaveDelayMs = 1500;

const int32_t kLogIntervals[] = {1, 2, 5, 10, 15, 30, 60, 120, 300};
const int32_t kSplitMinutes[] = {0, 5, 10, 30, 60, 120};
const int32_t kBlTimeouts[] = {0, 10, 20, 30, 60, 120, 300};
const int32_t kCycleSeconds[] = {0, 5, 10, 20, 30, 60};
const int32_t kGpsBauds[] = {4800, 9600, 19200, 38400, 57600, 115200};
const int32_t kSleepMv[] = {0, 3000, 3100, 3200, 3300};
const int32_t kTxPower[] = {8, 20, 34, 44, 52, 60, 68, 78};  // wifi_power_t, quarter dBm
const int32_t kIdleMinutes[] = {0, 5, 10, 30, 60};
const int32_t kCpuMhz[] = {80, 160};

SettingDef makeBool(const char *key, SettingGroup g, const char *label, bool def, const char *help) {
  SettingDef d = {key, g, SettingType::Bool, label, def, 0, 1, 1, 0, "", nullptr, nullptr, "", 0, false, help};
  return d;
}

SettingDef makeInt(const char *key, SettingGroup g, const char *label, int32_t def, int32_t min, int32_t max,
                   int32_t step, uint8_t decimals, const char *unit, const char *help) {
  SettingDef d = {key, g, SettingType::Int, label, def, min, max, step, decimals, unit, nullptr, nullptr, "", 0, false, help};
  return d;
}

SettingDef makeChoice(const char *key, SettingGroup g, const char *label, int32_t defIndex, const char *options,
                      const int32_t *values, const char *help) {
  SettingDef d = {key, g, SettingType::Choice, label, defIndex, 0, 0, 1, 0, "", options, values, "", 0, false, help};
  return d;
}

SettingDef makeText(const char *key, SettingGroup g, const char *label, const char *def, uint8_t maxLen, bool secret,
                    const char *help) {
  SettingDef d = {key, g, SettingType::Text, label, 0, 0, 0, 0, 0, "", nullptr, nullptr, def, maxLen, secret, help};
  return d;
}

const SettingDef kDefs[S_COUNT] = {
    // Logging
    makeBool("log_on", G_LOGGING, "Logging", true, "Record samples to flash"),
    makeChoice("log_int", G_LOGGING, "Interval", 2, "1 s|2 s|5 s|10 s|15 s|30 s|1 min|2 min|5 min", kLogIntervals,
               "Time between samples"),
    makeBool("log_nofix", G_LOGGING, "Log without fix", true, "Keep logging time, battery and satellites with no position"),
    makeInt("log_minspd", G_LOGGING, "Min speed", 0, 0, 30, 1, 0, "km/h", "Skip slower points (0 = off)"),
    makeInt("log_mindist", G_LOGGING, "Min distance", 0, 0, 200, 5, 0, "m", "Skip points closer than this (0 = off)"),
    makeChoice("log_split", G_LOGGING, "Split after gap", 3, "Off|5 min|10 min|30 min|1 h|2 h", kSplitMinutes,
               "Start a new session after a pause in logging"),
    makeChoice("log_full", G_LOGGING, "When full", 0, "Delete oldest|Stop logging", nullptr,
               "What to do when flash storage is full"),
    // Display
    makeInt("contrast", G_DISPLAY, "Contrast", 30, 0, 63, 1, 0, "", "LCD electronic volume"),
    makeInt("bl_level", G_DISPLAY, "Backlight", 100, 0, 100, 5, 0, "%", "Active brightness"),
    makeChoice("bl_timeout", G_DISPLAY, "Light timeout", 3, "Never|10 s|20 s|30 s|1 min|2 min|5 min", kBlTimeouts,
               "Dim the backlight after this idle time"),
    makeInt("bl_idle", G_DISPLAY, "Idle light", 0, 0, 50, 5, 0, "%", "Brightness after the timeout (0 = off)"),
    makeBool("rotate", G_DISPLAY, "Rotate 180", false, "Flip the picture"),
    makeBool("invert", G_DISPLAY, "Invert", false, "Light pixels on dark background"),
    makeChoice("start_scr", G_DISPLAY, "Start screen", 0,
               "Clock|Speed|GPS|Compass|Trip|Speed graph|Alt graph|Battery graph|Storage|System", nullptr,
               "Screen shown after boot"),
    makeChoice("autocycle", G_DISPLAY, "Auto cycle", 0, "Off|5 s|10 s|20 s|30 s|1 min", kCycleSeconds,
               "Rotate screens when idle"),
    makeBool("clock_sec", G_DISPLAY, "Clock seconds", true, "Show seconds on the clock"),
    makeBool("clock_12h", G_DISPLAY, "12-hour clock", false, "AM/PM time format"),
    makeChoice("bat_show", G_DISPLAY, "Battery shows", 0, "Voltage|Percent", nullptr,
               "Top-right of the status bar: cell voltage or charge percent"),
    // Screens
    makeBool("scr_clock", G_SCREENS, "Clock", true, "Big clock and date"),
    makeBool("scr_speed", G_SCREENS, "Speed", true, "Speedometer"),
    makeBool("scr_gps", G_SCREENS, "GPS", true, "Position and fix details"),
    makeBool("scr_compass", G_SCREENS, "Compass", true, "Heading"),
    makeBool("scr_trip", G_SCREENS, "Trip", true, "Trip statistics"),
    makeBool("scr_gspeed", G_SCREENS, "Speed graph", true, "Speed history"),
    makeBool("scr_galt", G_SCREENS, "Alt graph", true, "Altitude history"),
    makeBool("scr_gbat", G_SCREENS, "Battery graph", true, "Battery voltage history"),
    makeBool("scr_storage", G_SCREENS, "Storage", true, "Flash usage"),
    makeBool("scr_system", G_SCREENS, "System", true, "Uptime, memory, WiFi"),
    // Units & time
    makeChoice("units", G_UNITS, "Units", 0, "Metric|Imperial|Nautical", nullptr, "km/h+km+m, mph+mi+ft or kn+nm+m"),
    makeChoice("tz", G_UNITS, "Time zone", 1,
               "UTC|Israel|UK|Central Europe|Eastern Europe|Moscow|Dubai|India|China|Japan|Sydney|US Eastern|"
               "US Central|US Mountain|US Pacific|Brazil",
               nullptr, "Local time for display (logs are UTC)"),
    makeChoice("date_fmt", G_UNITS, "Date format", 0, "DD.MM.YYYY|YYYY-MM-DD|MM/DD/YYYY", nullptr, "Date style"),
    // GPS
    makeChoice("gps_baud", G_GPS, "Baud rate", 1, "4800|9600|19200|38400|57600|115200", kGpsBauds, "GPS module UART speed"),
    makeInt("gps_hdop", G_GPS, "Max HDOP", 40, 10, 100, 5, 1, "", "Ignore worse fixes in trip statistics"),
    makeInt("gps_moving", G_GPS, "Moving above", 20, 5, 100, 5, 1, "km/h", "Slower counts as standing still"),
    makeInt("gps_althyst", G_GPS, "Climb filter", 3, 1, 20, 1, 0, "m", "Altitude noise ignored for climb/descent"),
    // Battery
    makeInt("bat_cal", G_BATTERY, "Divider ratio", 2000, 1500, 2500, 5, 3, "", "(R1+R2)/R2, calibrate against a multimeter"),
    makeInt("bat_low", G_BATTERY, "Low warning", 340, 300, 380, 5, 2, "V", "Warn below this voltage"),
    makeChoice("bat_sleep", G_BATTERY, "Auto sleep", 0, "Off|3.00 V|3.10 V|3.20 V|3.30 V", kSleepMv,
               "Save logs and power off below this voltage"),
    makeInt("bat_mah", G_BATTERY, "Capacity", 2000, 100, 10000, 100, 0, "mAh", "Cell capacity for runtime estimate"),
    makeInt("bat_load", G_BATTERY, "Avg current", 60, 10, 400, 5, 0, "mA", "Assumed average draw for runtime estimate"),
    // WiFi
    makeChoice("wifi_mode", G_WIFI, "WiFi mode", 0, "Off|Access point|Home WiFi|AP + Home", nullptr,
               "Home WiFi falls back to the access point if it cannot connect"),
    makeText("ap_ssid", G_WIFI, "AP name", "GPS-LOGGER", 31, false, "Access point network name"),
    makeText("ap_pass", G_WIFI, "AP password", "12345678", 63, true, "At least 8 characters"),
    makeText("sta_ssid", G_WIFI, "Home SSID", "", 32, false, "Your router network name"),
    makeText("sta_pass", G_WIFI, "Home password", "", 63, true, "Your router password"),
    makeText("hostname", G_WIFI, "Hostname", "gpslogger", 24, false, "http://<hostname>.local on the home network"),
    makeChoice("wifi_txpwr", G_WIFI, "TX power", 2, "2 dBm|5 dBm|8.5 dBm|11 dBm|13 dBm|15 dBm|17 dBm|19.5 dBm", kTxPower,
               "Lower saves battery"),
    makeChoice("wifi_idle", G_WIFI, "Auto off", 2, "Never|5 min|10 min|30 min|1 h", kIdleMinutes,
               "Turn WiFi off with no clients and no web requests"),
    makeText("web_pass", G_WIFI, "Web password", "", 63, true, "Protect the web page (user: admin, empty = open)"),
    // System
    makeText("dev_name", G_SYSTEM, "Device name", "GPS Logger", 24, false, "Shown on the web page and in exports"),
    makeChoice("cpu_mhz", G_SYSTEM, "CPU speed", 0, "80 MHz|160 MHz", kCpuMhz, "80 MHz saves power"),
};

Preferences prefs;
int32_t ints[S_COUNT];
String texts[S_COUNT];
uint64_t dirty = 0;
uint32_t dirtySinceMs = 0;

int32_t clampValue(int id, int32_t v) {
  const SettingDef &d = kDefs[id];
  switch (d.type) {
    case SettingType::Bool:
      return v ? 1 : 0;
    case SettingType::Int:
      return constrain(v, d.min, d.max);
    case SettingType::Choice:
      return constrain(v, 0, Settings::optionCount(id) - 1);
    default:
      return 0;
  }
}

void markDirty(int id) {
  dirty |= 1ULL << id;
  dirtySinceMs = millis();
}

bool parseBool(String s, int32_t &out) {
  s.toLowerCase();
  if (s == "1" || s == "on" || s == "true" || s == "yes") {
    out = 1;
    return true;
  }
  if (s == "0" || s == "off" || s == "false" || s == "no") {
    out = 0;
    return true;
  }
  return false;
}

bool isNumber(const String &s) {
  if (s.isEmpty()) return false;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (!isdigit((unsigned char)c) && c != '.' && c != '-') return false;
  }
  return true;
}

}  // namespace

void Settings::begin() {
  static_assert(S_COUNT <= 64, "dirty mask holds 64 settings");
  prefs.begin("cfg", false);
  for (int i = 0; i < S_COUNT; i++) {
    const SettingDef &d = kDefs[i];
    if (d.type == SettingType::Text) {
      texts[i] = prefs.isKey(d.key) ? prefs.getString(d.key, d.textDef) : String(d.textDef);
    } else {
      ints[i] = clampValue(i, prefs.isKey(d.key) ? prefs.getInt(d.key, d.def) : d.def);
    }
  }
}

void Settings::loop() {
  if (dirty && millis() - dirtySinceMs >= kSaveDelayMs) saveNow();
}

void Settings::saveNow() {
  for (int i = 0; i < S_COUNT && dirty; i++) {
    if (!(dirty & (1ULL << i))) continue;
    const SettingDef &d = kDefs[i];
    if (d.type == SettingType::Text) {
      prefs.putString(d.key, texts[i]);
    } else {
      prefs.putInt(d.key, ints[i]);
    }
    dirty &= ~(1ULL << i);
  }
}

void Settings::factoryReset() {
  prefs.clear();
  dirty = 0;
  for (int i = 0; i < S_COUNT; i++) {
    const SettingDef &d = kDefs[i];
    if (d.type == SettingType::Text) {
      texts[i] = d.textDef;
    } else {
      ints[i] = clampValue(i, d.def);
    }
  }
}

const SettingDef &Settings::def(int id) { return kDefs[id]; }

int Settings::find(const char *key) {
  for (int i = 0; i < S_COUNT; i++) {
    if (strcmp(kDefs[i].key, key) == 0) return i;
  }
  return -1;
}

int32_t Settings::get(int id) { return ints[id]; }

int32_t Settings::value(int id) {
  const SettingDef &d = kDefs[id];
  return d.type == SettingType::Choice && d.values ? d.values[ints[id]] : ints[id];
}

float Settings::real(int id) {
  float v = ints[id];
  for (uint8_t i = 0; i < kDefs[id].decimals; i++) v /= 10;
  return v;
}

const String &Settings::text(int id) { return texts[id]; }

bool Settings::set(int id, int32_t v) {
  if (kDefs[id].type == SettingType::Text) return false;
  v = clampValue(id, v);
  if (ints[id] == v) return false;
  ints[id] = v;
  markDirty(id);
  return true;
}

bool Settings::setText(int id, const String &s) {
  const SettingDef &d = kDefs[id];
  if (d.type != SettingType::Text) return false;
  String v = s.substring(0, d.maxLen);
  if (texts[id] == v) return false;
  texts[id] = v;
  markDirty(id);
  return true;
}

bool Settings::parse(int id, const String &input) {
  const SettingDef &d = kDefs[id];
  String s = input;
  s.trim();
  switch (d.type) {
    case SettingType::Bool: {
      int32_t v;
      if (!parseBool(s, v)) return false;
      set(id, v);
      return true;
    }
    case SettingType::Int: {
      if (!isNumber(s)) return false;
      set(id, lround(s.toFloat() * pow(10, d.decimals)));
      return true;
    }
    case SettingType::Choice: {
      char label[32];
      for (int i = 0; i < optionCount(id); i++) {
        optionLabel(id, i, label, sizeof label);
        if (s.equalsIgnoreCase(label)) {
          set(id, i);
          return true;
        }
      }
      if (!isNumber(s)) return false;
      int32_t n = s.toInt();
      if (d.values) {
        for (int i = 0; i < optionCount(id); i++) {
          if (d.values[i] == n) {
            set(id, i);
            return true;
          }
        }
        return false;
      }
      if (n < 0 || n >= optionCount(id)) return false;
      set(id, n);
      return true;
    }
    case SettingType::Text:
      setText(id, input);
      return true;
  }
  return false;
}

bool Settings::step(int id, int dir) {
  const SettingDef &d = kDefs[id];
  switch (d.type) {
    case SettingType::Bool:
      return set(id, !ints[id]);
    case SettingType::Int:
      return set(id, ints[id] + dir * d.step);
    case SettingType::Choice:
      return set(id, ints[id] + dir);
    default:
      return false;
  }
}

void Settings::format(int id, char *out, size_t n) {
  const SettingDef &d = kDefs[id];
  switch (d.type) {
    case SettingType::Bool:
      snprintf(out, n, "%s", ints[id] ? "ON" : "OFF");
      break;
    case SettingType::Int: {
      bool tight = d.unit[0] == '\0' || strcmp(d.unit, "%") == 0;
      snprintf(out, n, "%.*f%s%s", d.decimals, real(id), tight ? "" : " ", d.unit);
      break;
    }
    case SettingType::Choice:
      optionLabel(id, ints[id], out, n);
      break;
    case SettingType::Text:
      if (d.secret) {
        snprintf(out, n, "%s", texts[id].isEmpty() ? "not set" : "****");
      } else {
        snprintf(out, n, "%s", texts[id].isEmpty() ? "-" : texts[id].c_str());
      }
      break;
  }
}

int Settings::optionCount(int id) {
  const char *p = kDefs[id].options;
  if (!p) return 0;
  int count = 1;
  for (; *p; p++) {
    if (*p == '|') count++;
  }
  return count;
}

void Settings::optionLabel(int id, int index, char *out, size_t n) {
  const char *p = kDefs[id].options;
  out[0] = '\0';
  if (!p || n == 0) return;
  for (int i = 0; i < index && *p; p++) {
    if (*p == '|') i++;
  }
  size_t len = 0;
  while (p[len] && p[len] != '|') len++;
  if (len >= n) len = n - 1;
  memcpy(out, p, len);
  out[len] = '\0';
}
