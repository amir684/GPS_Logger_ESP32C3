#include "console.h"

#include <Arduino.h>
#include <time.h>

#include "app.h"
#include "battery.h"
#include "config.h"
#include "export.h"
#include "gps.h"
#include "input.h"
#include "logger.h"
#include "net.h"
#include "settings.h"
#include "trip.h"
#include "ui.h"
#include "version.h"
#include "web.h"

namespace {

enum class Format { Csv, Kml, Gpx };

char line[128];
size_t lineLen = 0;

void printHelp() {
  Serial.println(FW_NAME " " FW_VERSION " commands:");
  Serial.println("  stat                     battery, gps, logging, wifi and trip status");
  Serial.println("  ls                       list sessions");
  Serial.println("  kml|gpx [NAME|last]      export a session (default: last)");
  Serial.println("  csv [NAME|last|all]      export CSV");
  Serial.println("  rm NAME | rm all         delete sessions");
  Serial.println("  settings [GROUP]         list settings (key = value)");
  Serial.println("  get KEY | set KEY VALUE  read or change a setting, e.g. set log_int 10");
  Serial.println("  defaults yes             factory reset settings and restart");
  Serial.println("  wifi on|off              quick WiFi toggle");
  Serial.println("  restart | sleep          reboot or deep sleep (push the wheel to wake)");
  Serial.println("hardware bring-up:");
  Serial.println("  hw                       pin map, jog key levels, battery pin, display settings");
  Serial.println("  bl PCT                   backlight brightness 0-100");
  Serial.println("  bltest                   cycle GPIO/PWM modes on the LED pin (push = exit)");
  Serial.println("  lcd on|off|frame         test pattern, held until a key press");
  Serial.println("  lcdinit                  re-send the LCD init sequence");
  Serial.println("  pin N 0|1                drive a GPIO (then 'restart' to restore pin functions)");
  Serial.println("remote:");
  Serial.println("  key up|down|push|hold    simulate the jog wheel");
  Serial.println("  screen N                 jump to dashboard screen N (0 = clock ... 9 = system)");
  Serial.println("  scr                      dump the LCD frame buffer as hex");
  Serial.println("  webtest [PATH]           HTTP GET to the device's own web server");
  Serial.println("  webdebug on|off          log every web request");
}

void dumpScreen() {
  const uint8_t *buf = Ui::frameBuffer();
  Serial.printf("# lcd 128 64 flip=%d invert=%d\n", Ui::frameFlipped(), (int)Settings::get(S_INVERT));
  for (int row = 0; row < 8; row++) {
    char line[257];
    for (int x = 0; x < 128; x++) snprintf(line + 2 * x, 3, "%02x", buf[row * 128 + x]);
    Serial.println(line);
  }
  Serial.println("# end");
}

Key parseKey(const char *name) {
  if (strcmp(name, "up") == 0) return Key::Up;
  if (strcmp(name, "down") == 0) return Key::Down;
  if (strcmp(name, "push") == 0) return Key::Push;
  if (strcmp(name, "hold") == 0) return Key::LongPush;
  return Key::None;
}

void printStatus() {
  TinyGPSPlus &g = Gps::raw();
  const TripStats &t = Trip::get();

  if (Battery::present()) {
    Serial.printf("battery  %.3f V (%d%%), runtime ~%d min\n", Battery::volts(), Battery::percent(),
                  Battery::runtimeMinutes());
  } else {
    Serial.printf("battery  none (pin reads %.3f V)\n", Battery::volts());
  }
  Serial.printf("gps      fix=%d sats=%u hdop=%.2f chars=%lu checksum_fail=%lu\n", Gps::hasFix(),
                (unsigned)g.satellites.value(), g.hdop.hdop(), (unsigned long)g.charsProcessed(),
                (unsigned long)g.failedChecksum());
  if (Gps::timeValid()) {
    time_t now = time(nullptr);
    tm lt;
    localtime_r(&now, &lt);
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &lt);
    Serial.printf("time     %s (local)\n", buf);
  } else {
    Serial.println("time     not set, waiting for GPS");
  }
  Serial.printf("logging  %s every %us, session %s, %lu records%s\n", Settings::get(S_LOGGING) ? "on" : "off",
                (unsigned)Settings::value(S_LOG_INTERVAL), Logger::currentSession().c_str(),
                (unsigned long)Logger::sessionRecords(), Logger::isFull() ? ", STORAGE FULL" : "");
  Serial.printf("storage  %u / %u KB, %d sessions\n", (unsigned)(Logger::usedBytes() / 1024),
                (unsigned)(Logger::totalBytes() / 1024), Logger::fileCount());
  Serial.printf("wifi     %s, ip %s, %d clients\n", Net::modeText(), Net::primaryIp().c_str(), Net::clients());
  Serial.printf("system   up %lus, heap %u KB (min %u), %.1f C, reset: %s\n", millis() / 1000,
                (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024), App::chipTemp(),
                App::resetReason());
  Serial.printf("trip     %.3f km, moving %lu s, speed avg %.1f sd %.1f max %.1f km/h, up %.0f m, down %.0f m\n",
                t.distanceM / 1000.0, (unsigned long)(t.movingMs / 1000), t.speedKmh.mean(), t.speedKmh.stddev(),
                t.speedKmh.maxVal(), t.climbM, t.descentM);
}

void printHardware() {
  Serial.printf("lcd      SCK=IO%d SDI=IO%d CS=IO%d DC=IO%d RST=IO%d LED=IO%d (%s)\n", PIN_LCD_SCK, PIN_LCD_SDI,
                PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_LED, LCD_LED_ACTIVE_LOW ? "active low" : "active high");
  Serial.printf("jog      UP=IO%d PUSH=IO%d DOWN=IO%d, levels up=%d push=%d down=%d (0 = pressed)\n", PIN_JOG_UP,
                PIN_JOG_PUSH, PIN_JOG_DOWN, digitalRead(PIN_JOG_UP), digitalRead(PIN_JOG_PUSH),
                digitalRead(PIN_JOG_DOWN));
  Serial.printf("battery  IO%d, %u mV at the pin, divider %.3f\n", PIN_BAT_ADC,
                (unsigned)analogReadMilliVolts(PIN_BAT_ADC), Settings::real(S_BAT_CAL));
  Serial.printf("gps      RX=IO%d, %u baud\n", PIN_GPS_RX, (unsigned)Settings::value(S_GPS_BAUD));
  Serial.printf("display  backlight %u%%, idle %u%%, contrast %u\n", (unsigned)Settings::get(S_BL_LEVEL),
                (unsigned)Settings::get(S_BL_IDLE), (unsigned)Settings::get(S_CONTRAST));
}

void listSessions() {
  std::vector<SessionInfo> list = Logger::sessions();
  for (const SessionInfo &s : list) {
    char start[24] = "-";
    if (s.records) {
      time_t t = s.firstEpoch;
      tm lt;
      localtime_r(&t, &lt);
      strftime(start, sizeof start, "%Y-%m-%d %H:%M", &lt);
    }
    uint32_t dur = s.lastEpoch - s.firstEpoch;
    Serial.printf("%s  %s local  %u:%02u:%02u  %6u pts  %5u KB%s\n", s.name.c_str(), start, (unsigned)(dur / 3600),
                  (unsigned)(dur / 60 % 60), (unsigned)(dur % 60), (unsigned)s.records, (unsigned)(s.bytes / 1024),
                  s.recording ? "  REC" : "");
  }
  Serial.printf("%u sessions, %u / %u KB used\n", (unsigned)list.size(), (unsigned)(Logger::usedBytes() / 1024),
                (unsigned)(Logger::totalBytes() / 1024));
}

void listSettings(const char *group) {
  char value[48];
  for (int g = 0; g < G_COUNT; g++) {
    if (group && *group && strncasecmp(Settings::kGroupNames[g], group, strlen(group)) != 0) continue;
    Serial.printf("[%s]\n", Settings::kGroupNames[g]);
    for (int i = 0; i < S_COUNT; i++) {
      const SettingDef &d = Settings::def(i);
      if (d.group != g) continue;
      Settings::format(i, value, sizeof value);
      Serial.printf("  %-12s = %-16s %s\n", d.key, value, d.label);
    }
  }
}

void printSetting(int id) {
  const SettingDef &d = Settings::def(id);
  char value[48];
  Settings::format(id, value, sizeof value);
  Serial.printf("%s = %s  (%s: %s)\n", d.key, value, d.label, d.help);
  if (d.type == SettingType::Choice) {
    char label[32];
    Serial.print("  options:");
    for (int i = 0; i < Settings::optionCount(id); i++) {
      Settings::optionLabel(id, i, label, sizeof label);
      Serial.printf(" [%s]", label);
    }
    Serial.println();
  } else if (d.type == SettingType::Int) {
    Serial.printf("  range %.*f .. %.*f\n", d.decimals, d.min / pow(10, d.decimals), d.decimals,
                  d.max / pow(10, d.decimals));
  }
}

void setSetting(char *arg) {
  char *value = arg ? strchr(arg, ' ') : nullptr;
  if (!value) {
    Serial.println("usage: set KEY VALUE");
    return;
  }
  *value++ = '\0';
  int id = Settings::find(arg);
  if (id < 0) {
    Serial.printf("unknown setting '%s', try 'settings'\n", arg);
    return;
  }
  char before[48], after[48];
  Settings::format(id, before, sizeof before);
  String oldText = Settings::text(id);
  if (!Settings::parse(id, value)) {
    Serial.printf("invalid value '%s'\n", value);
    printSetting(id);
    return;
  }
  Settings::format(id, after, sizeof after);
  if (strcmp(before, after) != 0 || oldText != Settings::text(id)) App::applySetting(id);
  Serial.printf("%s = %s\n", Settings::def(id).key, after);
}

// Exports start with "# file NAME" and end with "# end" so tools/export.py knows what to save
void exportSession(const char *arg, Format format) {
  if (format == Format::Csv && arg && strcmp(arg, "all") == 0) {
    Serial.println("# file GPS_all.csv");
    Serial.println(Export::kCsvHeader);
    uint32_t rows = 0;
    for (const SessionInfo &s : Logger::sessions()) {
      File f = Logger::openSession(s.name);
      if (f) rows += Export::csv(f, Serial);
    }
    Serial.printf("# end %u rows\n", (unsigned)rows);
    return;
  }

  String name = arg && *arg ? String(arg) : String("last");
  if (name == "last") name = Logger::latestSession();
  File f = Logger::openSession(name);
  if (!f) {
    Serial.printf("# session '%s' not found\n# end\n", name.c_str());
    return;
  }
  const char *ext = format == Format::Kml ? "kml" : format == Format::Gpx ? "gpx" : "csv";
  Serial.printf("# file GPS_%s.%s\n", name.c_str(), ext);
  switch (format) {
    case Format::Kml:
      Export::kml(f, name, Serial);
      break;
    case Format::Gpx:
      Export::gpx(f, name, Serial);
      break;
    case Format::Csv:
      Serial.println(Export::kCsvHeader);
      Export::csv(f, Serial);
      break;
  }
  Serial.println("# end");
}

void drivePin(const char *arg) {
  int pin = -1;
  int level = -1;
  // GPIO11-17 belong to the module's SPI flash, GPIO18/19 to USB
  bool parsed = sscanf(arg, "%d %d", &pin, &level) == 2;
  bool allowed = (pin >= 0 && pin <= 10) || pin == 20 || pin == 21;
  if (!parsed || !allowed || (level != 0 && level != 1)) {
    Serial.println("usage: pin N 0|1  (N = 0-10, 20, 21)");
    return;
  }
  ledcDetachPin(pin);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, level);
  Serial.printf("IO%d = %d, 'restart' to restore normal pin functions\n", pin, level);
}

bool is(const char *cmd, const char *name) { return strcmp(cmd, name) == 0; }

void execute(char *cmd) {
  char *arg = strchr(cmd, ' ');
  if (arg) {
    *arg++ = '\0';
    while (*arg == ' ') arg++;
  }
  bool hasArg = arg && *arg;

  if (is(cmd, "help")) {
    printHelp();
  } else if (is(cmd, "stat")) {
    printStatus();
  } else if (is(cmd, "ls")) {
    listSessions();
  } else if (is(cmd, "kml")) {
    exportSession(arg, Format::Kml);
  } else if (is(cmd, "gpx")) {
    exportSession(arg, Format::Gpx);
  } else if (is(cmd, "csv")) {
    exportSession(arg, Format::Csv);
  } else if (is(cmd, "rm") && hasArg && is(arg, "all")) {
    App::eraseLogs();
    Serial.println("all sessions deleted");
  } else if (is(cmd, "rm") && hasArg) {
    Serial.println(Logger::removeSession(arg) ? "deleted" : "not found");
  } else if (is(cmd, "settings")) {
    listSettings(arg);
  } else if (is(cmd, "get") && hasArg) {
    int id = Settings::find(arg);
    if (id < 0) {
      Serial.printf("unknown setting '%s'\n", arg);
    } else {
      printSetting(id);
    }
  } else if (is(cmd, "set")) {
    setSetting(arg);
  } else if (is(cmd, "defaults") && hasArg && is(arg, "yes")) {
    Serial.println("restoring defaults and restarting");
    App::factoryReset();
  } else if (is(cmd, "wifi") && hasArg && (is(arg, "on") || is(arg, "off"))) {
    App::setWifi(is(arg, "on"));
    Serial.printf("wifi %s\n", arg);
  } else if (is(cmd, "restart") || is(cmd, "reboot")) {
    Serial.println("restarting");
    App::restart(200);
  } else if (is(cmd, "sleep")) {
    Serial.println("going to deep sleep, push the wheel to wake");
    App::sleep(500);
  } else if (is(cmd, "hw")) {
    printHardware();
  } else if (is(cmd, "bl") && hasArg) {
    if (Settings::set(S_BL_LEVEL, atoi(arg))) App::applySetting(S_BL_LEVEL);
    Serial.printf("backlight %u%%\n", (unsigned)Settings::get(S_BL_LEVEL));
  } else if (is(cmd, "bltest")) {
    Ui::backlightTest();
  } else if (is(cmd, "lcd") && hasArg) {
    Ui::testPattern(is(arg, "on") ? 1 : is(arg, "frame") ? 2 : 0);
    Serial.println("test pattern shown until a key press");
  } else if (is(cmd, "lcdinit")) {
    Ui::reinit();
    Serial.println("lcd init sequence sent");
  } else if (is(cmd, "pin") && hasArg) {
    drivePin(arg);
  } else if (is(cmd, "webtest")) {
    Web::selfTest(hasArg ? arg : "/", Serial);
  } else if (is(cmd, "webdebug") && hasArg) {
    Web::setDebug(is(arg, "on"));
    Serial.printf("web request log %s\n", is(arg, "on") ? "on" : "off");
  } else if (is(cmd, "scr")) {
    dumpScreen();
  } else if (is(cmd, "key") && hasArg && parseKey(arg) != Key::None) {
    Input::inject(parseKey(arg));
    Serial.printf("key %s\n", arg);
  } else if (is(cmd, "screen") && hasArg) {
    Ui::setScreen(atoi(arg));
    Serial.printf("screen %d\n", atoi(arg));
  } else {
    Serial.printf("unknown command '%s', type 'help'\n", cmd);
  }
}

}  // namespace

void Console::update() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineLen) {
        line[lineLen] = '\0';
        execute(line);
        lineLen = 0;
      }
    } else if (lineLen < sizeof line - 1) {
      line[lineLen++] = c;
    }
  }
}
