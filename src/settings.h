#pragma once

#include <Arduino.h>

// Every user setting is described once in a table (settings.cpp). The device menu, the web page,
// the serial console and NVS persistence are all generated from that table.

enum class SettingType : uint8_t { Bool, Int, Choice, Text };

enum SettingGroup : uint8_t { G_LOGGING, G_DISPLAY, G_SCREENS, G_UNITS, G_GPS, G_BATTERY, G_WIFI, G_SYSTEM, G_COUNT };

enum SettingId : uint8_t {
  // Logging
  S_LOGGING,
  S_LOG_INTERVAL,
  S_LOG_NOFIX,
  S_LOG_MINSPD,
  S_LOG_MINDIST,
  S_LOG_SPLIT,
  S_LOG_FULL,
  S_AUTO_LOG,
  S_AUTO_START,
  S_AUTO_STARTT,
  S_AUTO_STOP,
  S_AUTO_STOPT,
  S_AUTO_TRIP,
  // Display
  S_CONTRAST,
  S_BL_LEVEL,
  S_BL_TIMEOUT,
  S_BL_IDLE,
  S_BL_FADE,
  S_ROTATE,
  S_INVERT,
  S_START_SCREEN,
  S_AUTOCYCLE,
  S_CLOCK_SEC,
  S_CLOCK_12H,
  S_BAT_SHOW,
  // Screens (order matches Screen in ui.cpp)
  S_SCR_CLOCK,
  S_SCR_SPEED,
  S_SCR_GPS,
  S_SCR_COMPASS,
  S_SCR_TRIP,
  S_SCR_GSPEED,
  S_SCR_GALT,
  S_SCR_GBAT,
  S_SCR_STORAGE,
  S_SCR_SYSTEM,
  // Units & time
  S_UNITS,
  S_TZ,
  S_DATE_FMT,
  // GPS
  S_GPS_BAUD,
  S_GPS_MAXHDOP,
  S_GPS_MOVING,
  S_GPS_ALTHYST,
  S_GPS_SLEEP,
  // Battery
  S_BAT_CAL,
  S_BAT_LOW,
  S_BAT_SLEEP,
  S_BAT_MAH,
  S_BAT_LOAD,
  // WiFi
  S_WIFI_MODE,
  S_AP_SSID,
  S_AP_PASS,
  S_STA_SSID,
  S_STA_PASS,
  S_HOSTNAME,
  S_WIFI_TXPWR,
  S_WIFI_IDLE,
  S_WEB_PASS,
  // System
  S_DEV_NAME,
  S_CPU_MHZ,
  S_SLEEP_IDLE,
  S_COUNT
};

constexpr int kScreenSettingFirst = S_SCR_CLOCK;
constexpr int kScreenCount = S_SCR_SYSTEM - S_SCR_CLOCK + 1;

enum WifiModeSetting : uint8_t { WM_OFF, WM_AP, WM_HOME, WM_BOTH };
enum UnitSystem : uint8_t { UNITS_METRIC, UNITS_IMPERIAL, UNITS_NAUTICAL };

struct SettingDef {
  const char *key;  // NVS key and JSON name, max 15 chars
  SettingGroup group;
  SettingType type;
  const char *label;
  int32_t def;  // Bool/Int value, Choice index
  int32_t min;
  int32_t max;
  int32_t step;
  uint8_t decimals;        // Int: stored value = real value * 10^decimals
  const char *unit;        // Int
  const char *options;     // Choice: labels separated by '|'
  const int32_t *values;   // Choice: value per option, nullptr = option index
  const char *textDef;     // Text
  uint8_t maxLen;          // Text
  bool secret;             // Text: never shown or sent back
  const char *help;
};

namespace Settings {
extern const char *const kGroupNames[G_COUNT];

void begin();  // load from NVS
void loop();   // write pending changes after a short debounce
void saveNow();
void factoryReset();  // clear NVS and restore defaults

const SettingDef &def(int id);
int find(const char *key);  // -1 if unknown

int32_t get(int id);    // Bool/Int stored value, Choice index
int32_t value(int id);  // Choice mapped value, otherwise get()
float real(int id);     // Int value divided by 10^decimals
const String &text(int id);

bool set(int id, int32_t v);  // clamped; true if the value changed
bool setText(int id, const String &s);
bool parse(int id, const String &input);  // human input: "on", "3.4", "30 s", a label...
bool step(int id, int dir);               // one menu step up/down
void format(int id, char *out, size_t n);  // display string with unit

int optionCount(int id);
void optionLabel(int id, int index, char *out, size_t n);
}  // namespace Settings
