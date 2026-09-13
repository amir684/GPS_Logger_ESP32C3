#include "ui.h"

#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <driver/gpio.h>
#include <time.h>

#include <algorithm>

#include "app.h"
#include "battery.h"
#include "config.h"
#include "gps.h"
#include "input.h"
#include "logger.h"
#include "menu.h"
#include "net.h"
#include "samples.h"
#include "settings.h"
#include "stats.h"
#include "trip.h"
#include "units.h"
#include "version.h"

namespace {

// Same panel, constructor and rotation as the STM32 mini console (flex cable below the glass)
U8G2_ST7567_JLX12864_F_4W_HW_SPI lcd(U8G2_R2, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

constexpr uint8_t kBacklightChannel = 0;
constexpr uint32_t kDrawPeriodMs = 250;
constexpr uint32_t kMenuTimeoutMs = 60000;
constexpr uint32_t kResetConfirmMs = 3000;

// Order matches the S_SCR_* settings and the "start_scr" options
enum Screen : uint8_t {
  ScrClock,
  ScrSpeed,
  ScrGps,
  ScrCompass,
  ScrTrip,
  ScrSpeedGraph,
  ScrAltGraph,
  ScrBatteryGraph,
  ScrStorage,
  ScrSystem,
  ScrCount
};

int screen = ScrClock;
bool dimmed = false;
float backlightCurrent = 0;  // faded towards backlightTarget, both 0-255
int backlightTarget = 0;
uint32_t lastFadeMs = 0;
bool showStats = false;
bool redraw = true;
bool testHold = false;  // console test pattern stays on screen until a key press
bool lowBatteryWarned = false;
uint32_t resetArmedMs = 0;
uint32_t lastDrawMs = 0;
uint32_t lastCycleMs = 0;
uint32_t toastUntilMs = 0;
char toastText[28] = "";

void writeBacklight(uint8_t brightness) {
  ledcWrite(kBacklightChannel, LCD_LED_ACTIVE_LOW ? 255 - brightness : brightness);
}

void setBacklightTarget(bool full) {
  dimmed = !full;
  backlightTarget = (full ? Settings::get(S_BL_LEVEL) : Settings::get(S_BL_IDLE)) * 255 / 100;
}

// Jump to the target without fading: at boot, after the console test, or while editing the brightness
void snapBacklight() {
  backlightCurrent = backlightTarget;
  writeBacklight((uint8_t)lroundf(backlightCurrent));
}

void updateBacklightFade() {
  uint32_t now = millis();
  uint32_t elapsed = now - lastFadeMs;
  lastFadeMs = now;
  if (backlightCurrent == (float)backlightTarget) return;

  uint32_t fadeMs = Settings::value(S_BL_FADE);
  if (fadeMs == 0 || elapsed >= fadeMs) {
    backlightCurrent = backlightTarget;
  } else {
    float step = 255.0f * elapsed / fadeMs;  // a full sweep takes the configured time
    if (backlightCurrent < backlightTarget) {
      backlightCurrent = std::min((float)backlightTarget, backlightCurrent + step);
    } else {
      backlightCurrent = std::max((float)backlightTarget, backlightCurrent - step);
    }
  }
  writeBacklight((uint8_t)lroundf(backlightCurrent));
}

bool screenEnabled(int s) { return Settings::get(kScreenSettingFirst + s); }

int nextScreen(int from, int dir) {
  for (int i = 1; i <= ScrCount; i++) {
    int s = (from + dir * i + 2 * ScrCount) % ScrCount;
    if (screenEnabled(s)) return s;
  }
  return ScrClock;
}

void drawCentered(const char *text, int y) { lcd.drawStr((128 - lcd.getStrWidth(text)) / 2, y, text); }

void formatDuration(uint32_t seconds, char *out, size_t n) {
  if (seconds < 120) {
    snprintf(out, n, "%us", (unsigned)seconds);
  } else if (seconds < 7200) {
    snprintf(out, n, "%um", (unsigned)(seconds / 60));
  } else {
    snprintf(out, n, "%.1fh", seconds / 3600.0);
  }
}

double identity(double v) { return v; }

// ---------------------------------------------------------------- status bar

void drawStatusBar(const char *title) {
  char buf[16];
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(0, 7, title);

  int x = 128;
  if (Battery::present()) {
    int pct = Battery::percent();
    x -= 13;
    bool blinkOff = Battery::low() && (millis() / 500) % 2;
    if (!blinkOff) {
      lcd.drawFrame(x, 1, 11, 7);
      lcd.drawBox(x + 11, 3, 2, 3);
      lcd.drawBox(x + 2, 3, (7 * pct + 50) / 100, 3);
    }
    if (Settings::get(S_BAT_SHOW) == 1) {
      snprintf(buf, sizeof buf, "%d%%", pct);
    } else {
      snprintf(buf, sizeof buf, "%.2fV", Battery::volts());
    }
  } else {
    snprintf(buf, sizeof buf, "USB");
  }
  x -= lcd.getStrWidth(buf) + 2;
  lcd.drawStr(x, 7, buf);

  snprintf(buf, sizeof buf, "%c%u", Gps::hasFix() ? '*' : '-', (unsigned)Gps::raw().satellites.value());
  x -= lcd.getStrWidth(buf) + 4;
  lcd.drawStr(x, 7, buf);

  if (Net::active()) {
    x -= 8;
    lcd.drawStr(x, 7, "W");
  }
  if (Settings::get(S_LOGGING)) {
    x -= 8;
    if (Logger::isFull()) {
      lcd.drawStr(x, 7, "F");
    } else if (Gps::timeValid()) {
      lcd.drawDisc(x + 2, 4, 2);
    } else {
      lcd.drawCircle(x + 2, 4, 2);  // armed, waiting for GPS time
    }
  }
  lcd.drawHLine(0, 9, 128);
}

// ---------------------------------------------------------------- screens

void drawClock() {
  drawStatusBar("CLOCK");
  char buf[32];
  if (!Gps::timeValid()) {
    lcd.setFont(u8g2_font_6x10_tf);
    drawCentered("Waiting for GPS", 32);
    snprintf(buf, sizeof buf, "NMEA chars %lu", (unsigned long)Gps::raw().charsProcessed());
    drawCentered(buf, 46);
    return;
  }

  time_t now = time(nullptr);
  tm lt;
  localtime_r(&now, &lt);
  bool seconds = Settings::get(S_CLOCK_SEC);

  Units::formatTime(lt, false, buf, sizeof buf);
  lcd.setFont(u8g2_font_logisoso24_tn);
  int w = lcd.getStrWidth(buf);
  int x = (128 - w - (seconds ? 14 : 0)) / 2;
  lcd.drawStr(x, 38, buf);
  lcd.setFont(u8g2_font_6x10_tf);
  if (seconds) {
    snprintf(buf, sizeof buf, "%02d", lt.tm_sec);
    lcd.drawStr(x + w + 2, 38, buf);
  }
  if (Settings::get(S_CLOCK_12H)) lcd.drawStr(x + w + 2, 26, lt.tm_hour < 12 ? "AM" : "PM");

  Units::formatDate(lt, buf, sizeof buf);
  drawCentered(buf, 51);

  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(buf, sizeof buf, "%s   REC %lu", Gps::hasFix() ? "FIX" : "NO FIX", (unsigned long)Logger::sessionRecords());
  drawCentered(buf, 62);
}

void drawSpeed() {
  drawStatusBar("SPEED");
  TinyGPSPlus &g = Gps::raw();
  const TripStats &t = Trip::get();
  char buf[40];

  lcd.setFont(u8g2_font_logisoso32_tn);
  if (Gps::hasFix()) {
    double v = Units::speed(g.speed.kmph());
    snprintf(buf, sizeof buf, v < 100 ? "%.1f" : "%.0f", v);
  } else {
    snprintf(buf, sizeof buf, "--");
  }
  int w = lcd.getStrWidth(buf);
  lcd.drawStr((100 - w) / 2, 47, buf);
  lcd.setFont(u8g2_font_6x10_tf);
  lcd.drawStr(127 - lcd.getStrWidth(Units::speedUnit()), 47, Units::speedUnit());

  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(buf, sizeof buf, "MAX %.1f  AVG %.1f", Units::speed(t.speedKmh.maxVal()), Units::speed(t.speedKmh.mean()));
  lcd.drawStr(0, 62, buf);
  if (Gps::hasFix() && g.speed.kmph() >= Settings::real(S_GPS_MOVING)) {
    const char *dir = Units::cardinal(g.course.deg());
    lcd.drawStr(127 - lcd.getStrWidth(dir), 62, dir);
  }
}

void drawGps() {
  drawStatusBar("GPS");
  TinyGPSPlus &g = Gps::raw();
  char buf[40];
  lcd.setFont(u8g2_font_6x10_tf);
  if (g.location.isValid()) {
    snprintf(buf, sizeof buf, "LAT %.6f", g.location.lat());
    lcd.drawStr(0, 20, buf);
    snprintf(buf, sizeof buf, "LON %.6f", g.location.lng());
    lcd.drawStr(0, 31, buf);
  } else {
    lcd.drawStr(0, 20, "LAT ---");
    lcd.drawStr(0, 31, "LON ---");
  }
  snprintf(buf, sizeof buf, "ALT %.0f%s HDOP %.1f", Units::altitude(g.altitude.meters()), Units::altitudeUnit(),
           g.hdop.hdop());
  lcd.drawStr(0, 42, buf);
  snprintf(buf, sizeof buf, "SPD %.1f %s %.0f\xb0", Units::speed(g.speed.kmph()), Units::speedUnit(), g.course.deg());
  lcd.drawStr(0, 53, buf);
  if (g.location.isValid()) {
    snprintf(buf, sizeof buf, "SAT %u  AGE %.1fs", (unsigned)g.satellites.value(), g.location.age() / 1000.0);
  } else {
    snprintf(buf, sizeof buf, "SAT %u  NMEA %lu", (unsigned)g.satellites.value(), (unsigned long)g.charsProcessed());
  }
  lcd.drawStr(0, 63, buf);
}

void drawCompass() {
  drawStatusBar("COMPASS");
  TinyGPSPlus &g = Gps::raw();
  constexpr int cx = 31, cy = 37, r = 25;
  char buf[24];

  lcd.drawCircle(cx, cy, r);
  for (int a = 0; a < 360; a += 30) {
    double rad = a * PI / 180.0;
    lcd.drawLine(cx + sin(rad) * (r - 3), cy - cos(rad) * (r - 3), cx + sin(rad) * r, cy - cos(rad) * r);
  }
  lcd.setFont(u8g2_font_4x6_tf);
  lcd.drawStr(cx - 1, cy - r + 10, "N");
  lcd.drawStr(cx - 1, cy + r - 4, "S");
  lcd.drawStr(cx - r + 5, cy + 3, "W");
  lcd.drawStr(cx + r - 8, cy + 3, "E");

  bool moving = Gps::hasFix() && g.course.isValid() && g.speed.kmph() >= Settings::real(S_GPS_MOVING);
  if (moving) {
    double rad = g.course.deg() * PI / 180.0;
    double left = rad + 2.5, right = rad - 2.5;
    lcd.drawTriangle(cx + sin(rad) * (r - 7), cy - cos(rad) * (r - 7), cx + sin(left) * 9, cy - cos(left) * 9,
                     cx + sin(right) * 9, cy - cos(right) * 9);
  } else {
    lcd.drawDisc(cx, cy, 2);
  }

  lcd.setFont(u8g2_font_logisoso16_tr);
  if (moving) {
    snprintf(buf, sizeof buf, "%03.0f", g.course.deg());
  } else {
    snprintf(buf, sizeof buf, "---");
  }
  lcd.drawStr(66, 34, buf);
  lcd.drawCircle(66 + lcd.getStrWidth(buf) + 3, 20, 2);

  lcd.setFont(u8g2_font_6x10_tf);
  lcd.drawStr(66, 48, moving ? Units::cardinal(g.course.deg()) : "STILL");
  lcd.setFont(u8g2_font_5x8_tf);
  snprintf(buf, sizeof buf, "%.1f %s", Units::speed(Gps::hasFix() ? g.speed.kmph() : 0), Units::speedUnit());
  lcd.drawStr(66, 62, buf);
}

void drawTrip() {
  drawStatusBar("TRIP");
  const TripStats &t = Trip::get();
  char buf[40];
  lcd.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof buf, "DIST   %.2f %s", Units::distance(t.distanceM), Units::distanceUnit());
  lcd.drawStr(0, 20, buf);
  unsigned long s = t.movingMs / 1000;
  snprintf(buf, sizeof buf, "MOVING %lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
  lcd.drawStr(0, 31, buf);
  snprintf(buf, sizeof buf, "AVG %.1f MAX %.1f", Units::speed(t.speedKmh.mean()), Units::speed(t.speedKmh.maxVal()));
  lcd.drawStr(0, 42, buf);
  snprintf(buf, sizeof buf, "UP %.0f%s DOWN %.0f%s", Units::altitude(t.climbM), Units::altitudeUnit(),
           Units::altitude(t.descentM), Units::altitudeUnit());
  lcd.drawStr(0, 53, buf);
  bool armed = resetArmedMs && millis() - resetArmedMs < kResetConfirmMs;
  if (armed) {
    lcd.drawStr(0, 63, "PUSH AGAIN = RESET");
  } else {
    snprintf(buf, sizeof buf, "%s  push: reset", Units::speedUnit());
    lcd.drawStr(0, 63, buf);
  }
}

void drawGraph(const char *title, const History<kHistorySize> &h, uint32_t periodS, int decimals,
               double (*convert)(double)) {
  drawStatusBar(title);
  char buf[32];
  size_t n = h.size();
  if (n < 2) {
    lcd.setFont(u8g2_font_6x10_tf);
    drawCentered("collecting...", 40);
    return;
  }

  RunningStats st;
  for (size_t i = 0; i < n; i++) st.add(convert(h.at(i)));
  char span[12];
  formatDuration((n - 1) * periodS, span, sizeof span);

  if (showStats) {
    lcd.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof buf, "N %u  SPAN %s", (unsigned)n, span);
    lcd.drawStr(0, 20, buf);
    snprintf(buf, sizeof buf, "LAST %.*f", decimals, convert(h.at(n - 1)));
    lcd.drawStr(0, 31, buf);
    snprintf(buf, sizeof buf, "MIN %.*f MAX %.*f", decimals, st.minVal(), decimals, st.maxVal());
    lcd.drawStr(0, 42, buf);
    snprintf(buf, sizeof buf, "MEAN %.*f", decimals + 1, st.mean());
    lcd.drawStr(0, 53, buf);
    snprintf(buf, sizeof buf, "STD  %.*f", decimals + 1, st.stddev());
    lcd.drawStr(0, 63, buf);
    return;
  }

  double lo = st.minVal();
  double hi = st.maxVal();
  if (hi - lo < 1e-6) {
    lo -= 1;
    hi += 1;
  }
  constexpr int gx = 0, gy = 11, gw = 100, gh = 53;
  for (int y = gy; y < gy + gh; y += 13) {
    for (int x = gx; x < gx + gw; x += 4) lcd.drawPixel(x, y);
  }
  int px = 0, py = 0;
  for (size_t i = 0; i < n; i++) {
    int x = gx + i * (gw - 1) / (n - 1);
    int y = gy + gh - 1 - (int)lround((convert(h.at(i)) - lo) / (hi - lo) * (gh - 1));
    if (i) lcd.drawLine(px, py, x, y);
    px = x;
    py = y;
  }
  lcd.drawVLine(gx + gw + 1, gy, gh);

  lcd.setFont(u8g2_font_4x6_tf);
  int lx = gx + gw + 4;
  snprintf(buf, sizeof buf, "%.*f", decimals, hi);
  lcd.drawStr(lx, gy + 6, buf);
  snprintf(buf, sizeof buf, "%.*f", decimals, convert(h.at(n - 1)));
  lcd.drawBox(lx - 1, gy + 18, lcd.getStrWidth(buf) + 2, 7);
  lcd.setDrawColor(0);
  lcd.drawStr(lx, gy + 24, buf);
  lcd.setDrawColor(1);
  lcd.drawStr(lx, gy + 38, span);
  snprintf(buf, sizeof buf, "%.*f", decimals, lo);
  lcd.drawStr(lx, gy + gh, buf);
}

void drawStorage() {
  drawStatusBar("STORAGE");
  char buf[40];
  size_t total = Logger::totalBytes();
  size_t used = Logger::usedBytes();
  lcd.setFont(u8g2_font_6x10_tf);
  snprintf(buf, sizeof buf, "%.2f / %.1f MB", used / 1048576.0, total / 1048576.0);
  lcd.drawStr(0, 20, buf);
  lcd.drawFrame(0, 23, 128, 7);
  if (total) lcd.drawBox(2, 25, (int)(124ULL * used / total), 3);
  if (Logger::isFull()) {
    lcd.drawStr(0, 42, "FULL: logging stopped");
  } else {
    snprintf(buf, sizeof buf, "SESS %d REC %lu", Logger::fileCount(), (unsigned long)Logger::sessionRecords());
    lcd.drawStr(0, 42, buf);
  }
  double bytesPerDay = sizeof(LogRecord) * 86400.0 / Settings::value(S_LOG_INTERVAL);
  double freeBytes = total * 0.9 - used;
  snprintf(buf, sizeof buf, "FREE ~%.0f days @%us", freeBytes > 0 ? freeBytes / bytesPerDay : 0.0,
           (unsigned)Settings::value(S_LOG_INTERVAL));
  lcd.drawStr(0, 53, buf);
  if (Net::active()) {
    String ip = Net::primaryIp();
    snprintf(buf, sizeof buf, "WiFi %s", ip.isEmpty() ? "connecting" : ip.c_str());
  } else {
    snprintf(buf, sizeof buf, "push: WiFi on");
  }
  lcd.drawStr(0, 63, buf);
}

void drawSystem() {
  drawStatusBar("SYSTEM");
  char buf[40];
  lcd.setFont(u8g2_font_5x8_tf);
  unsigned long s = millis() / 1000;
  snprintf(buf, sizeof buf, "UP %lud %02lu:%02lu:%02lu", s / 86400, s / 3600 % 24, s / 60 % 60, s % 60);
  lcd.drawStr(0, 18, buf);
  snprintf(buf, sizeof buf, "HEAP %uKB  %.1fC", (unsigned)(ESP.getFreeHeap() / 1024), App::chipTemp());
  lcd.drawStr(0, 27, buf);
  snprintf(buf, sizeof buf, "RESET %s", App::resetReason());
  lcd.drawStr(0, 36, buf);
  String ip = Net::primaryIp();
  snprintf(buf, sizeof buf, "WIFI %s", Net::active() ? (ip.isEmpty() ? Net::modeText() : ip.c_str()) : "off");
  lcd.drawStr(0, 45, buf);
  snprintf(buf, sizeof buf, "CLIENTS %d  CPU %uMHz", Net::clients(), (unsigned)getCpuFrequencyMhz());
  lcd.drawStr(0, 54, buf);
  snprintf(buf, sizeof buf, "FW %s", FW_VERSION);
  lcd.drawStr(0, 63, buf);
}

void drawToast() {
  if (millis() >= toastUntilMs) return;
  lcd.setFont(u8g2_font_6x10_tf);
  int w = lcd.getStrWidth(toastText) + 10;
  int x = (128 - w) / 2;
  lcd.setDrawColor(0);
  lcd.drawBox(x - 1, 23, w + 2, 20);
  lcd.setDrawColor(1);
  lcd.drawRFrame(x, 24, w, 18, 3);
  lcd.drawStr(x + 5, 37, toastText);
}

// While the push button is held past the long-press time: release opens the menu, keep holding to sleep
void drawHoldOverlay() {
  uint32_t held = Input::pushHoldMs();
  if (held < kLongPressMs) return;
  uint32_t span = kSleepHoldMs - kLongPressMs;
  uint32_t progress = std::min(held - kLongPressMs, span);

  lcd.setDrawColor(0);
  lcd.drawBox(6, 15, 116, 44);
  lcd.setDrawColor(1);
  lcd.drawRFrame(6, 15, 116, 44, 3);
  lcd.setFont(u8g2_font_6x10_tf);
  drawCentered(Menu::isOpen() ? "Release: back" : "Release: menu", 28);
  lcd.setFont(u8g2_font_5x8_tf);
  char buf[24];
  snprintf(buf, sizeof buf, "Keep holding: sleep %.1fs", (span - progress) / 1000.0);
  drawCentered(buf, 40);
  lcd.drawFrame(14, 45, 100, 8);
  lcd.drawBox(16, 47, 96 * progress / span, 4);
}

void draw() {
  char title[20];
  lcd.clearBuffer();
  if (Menu::isOpen()) {
    Menu::draw(lcd);
  } else {
    switch (screen) {
      case ScrClock: drawClock(); break;
      case ScrSpeed: drawSpeed(); break;
      case ScrGps: drawGps(); break;
      case ScrCompass: drawCompass(); break;
      case ScrTrip: drawTrip(); break;
      case ScrSpeedGraph:
        snprintf(title, sizeof title, "SPEED %s", Units::speedUnit());
        drawGraph(title, histSpeed, Settings::value(S_LOG_INTERVAL), 1, Units::speed);
        break;
      case ScrAltGraph:
        snprintf(title, sizeof title, "ALT %s", Units::altitudeUnit());
        drawGraph(title, histAlt, Settings::value(S_LOG_INTERVAL), 0, Units::altitude);
        break;
      case ScrBatteryGraph: drawGraph("BATTERY V", histBattery, kBatteryGraphPeriodS, 2, identity); break;
      case ScrStorage: drawStorage(); break;
      case ScrSystem: drawSystem(); break;
      default: break;
    }
  }
  drawToast();
  drawHoldOverlay();
  lcd.sendBuffer();
}

// ---------------------------------------------------------------- input

void onScreenPush() {
  switch (screen) {
    case ScrTrip:
      if (resetArmedMs && millis() - resetArmedMs < kResetConfirmMs) {
        App::resetTrip();
        resetArmedMs = 0;
      } else {
        resetArmedMs = millis();
      }
      break;
    case ScrSpeedGraph:
    case ScrAltGraph:
    case ScrBatteryGraph:
      showStats = !showStats;
      break;
    case ScrStorage:
      App::setWifi(!Net::active());
      break;
    default:
      Menu::open();
      break;
  }
}

void handleScreenKey(Key key) {
  switch (key) {
    case Key::Up:
    case Key::Down:
      screen = nextScreen(screen, key == Key::Up ? -1 : 1);
      showStats = false;
      resetArmedMs = 0;
      break;
    case Key::Push:
      onScreenPush();
      break;
    case Key::LongPush:
      Menu::open();
      break;
    default:
      break;
  }
}

}  // namespace

void Ui::begin() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_LCD_LED);  // held off during deep sleep
  ledcSetup(kBacklightChannel, 5000, 8);
  ledcAttachPin(PIN_LCD_LED, kBacklightChannel);
  // Strongest drive: the pin sinks the backlight current directly
  gpio_set_drive_capability((gpio_num_t)PIN_LCD_LED, GPIO_DRIVE_CAP_3);

  // Must run before lcd.begin(): U8g2 calls SPI.begin() with default pins, which is ignored once the bus is up
  SPI.begin(PIN_LCD_SCK, -1, PIN_LCD_SDI, -1);
  lcd.begin();
  applyDisplay();

  lcd.clearBuffer();
  lcd.drawRFrame(0, 0, 128, 64, 4);
  lcd.setFont(u8g2_font_logisoso16_tr);
  drawCentered(FW_NAME, 30);
  lcd.setFont(u8g2_font_5x8_tf);
  char buf[32];
  snprintf(buf, sizeof buf, "v%s  %u MB flash", FW_VERSION, (unsigned)(ESP.getFlashChipSize() / 1048576));
  drawCentered(buf, 46);
  drawCentered(App::wokeFromSleep() ? "woke from sleep" : App::resetReason(), 57);
  lcd.sendBuffer();
  delay(1200);

  screen = Settings::get(S_START_SCREEN);
  if (!screenEnabled(screen)) screen = nextScreen(screen, 1);
}

void Ui::update() {
  Key key = Input::poll();
  uint32_t now = millis();  // after poll(), which stamps the last activity time

  if (key == Key::SleepHold) {
    App::sleep(0);  // works even with the backlight off
  } else if (key != Key::None) {
    // With the backlight fully off, the first press only wakes the screen
    bool wakeOnly = dimmed && Settings::get(S_BL_IDLE) == 0 && Settings::get(S_BL_LEVEL) > 0;
    if (dimmed) setBacklightTarget(true);
    testHold = false;
    lastCycleMs = now;
    if (!wakeOnly) {
      if (Menu::isOpen()) {
        Menu::handleKey(key);
      } else {
        handleScreenKey(key);
      }
    }
    redraw = true;
  } else {
    uint32_t idle = now - Input::lastActivityMs();
    uint32_t timeout = Settings::value(S_BL_TIMEOUT) * 1000UL;
    if (!dimmed && timeout && idle >= timeout) setBacklightTarget(false);
    if (Menu::isOpen() && idle >= kMenuTimeoutMs) {
      Menu::close();
      redraw = true;
    }
    uint32_t cycle = Settings::value(S_AUTOCYCLE) * 1000UL;
    if (cycle && !Menu::isOpen() && idle >= cycle && now - lastCycleMs >= cycle) {
      lastCycleMs = now;
      screen = nextScreen(screen, 1);
      showStats = false;
      redraw = true;
    }
  }

  if (Battery::low()) {
    if (!lowBatteryWarned) toast("Battery low");
    lowBatteryWarned = true;
  } else if (Battery::volts() > Settings::real(S_BAT_LOW) + 0.05f) {
    lowBatteryWarned = false;
  }

  updateBacklightFade();

  if (testHold) return;
  bool holding = Input::pushHoldMs() >= kLongPressMs;  // animate the sleep countdown smoothly
  if (redraw || now - lastDrawMs >= (holding ? 60 : kDrawPeriodMs)) {
    redraw = false;
    lastDrawMs = now;
    draw();
  }
}

void Ui::applyDisplay() {
  lcd.setContrast(Settings::get(S_CONTRAST) * 4);
  lcd.setDisplayRotation(Settings::get(S_ROTATE) ? U8G2_R0 : U8G2_R2);
  lcd.sendF("c", Settings::get(S_INVERT) ? 0xA7 : 0xA6);  // ST7567 reverse display
  setBacklightTarget(!dimmed);
  snapBacklight();  // brightness edits should show immediately, not fade
  redraw = true;
}

void Ui::toast(const char *text) {
  strlcpy(toastText, text, sizeof toastText);
  toastUntilMs = millis() + 1500;
  redraw = true;
}

void Ui::showMessage(const char *title, const char *detail) {
  lcd.clearBuffer();
  lcd.drawRFrame(0, 0, 128, 64, 4);
  lcd.setFont(u8g2_font_logisoso16_tr);
  drawCentered(title, 32);
  lcd.setFont(u8g2_font_5x8_tf);
  drawCentered(detail, 48);
  lcd.sendBuffer();
}

void Ui::shutdown() {
  backlightTarget = 0;
  snapBacklight();
  lcd.setPowerSave(1);
}

void Ui::testPattern(int mode) {
  lcd.clearBuffer();
  if (mode == 1) {
    lcd.drawBox(0, 0, 128, 64);
  } else if (mode == 2) {
    lcd.drawFrame(0, 0, 128, 64);
    for (int y = 4; y < 60; y += 8) {
      for (int x = 4; x < 124; x += 8) {
        if (((x + y) / 8) % 2) lcd.drawBox(x, y, 4, 4);
      }
    }
  }
  lcd.sendBuffer();
  testHold = true;
}

void Ui::reinit() {
  lcd.begin();
  applyDisplay();
}

void Ui::backlightTest() {
  struct Phase {
    const char *name;
    bool pwm;
    uint8_t value;  // GPIO level or brightness
  };
  const Phase phases[] = {
      {"GPIO HIGH", false, 1}, {"GPIO LOW", false, 0}, {"PWM 100%", true, 255}, {"PWM 50%", true, 128}, {"PWM 10%", true, 26},
  };
  constexpr int kPhases = sizeof(phases) / sizeof(phases[0]);
  constexpr uint32_t kPhaseMs = 3000;
  constexpr uint32_t kMaxMs = 5 * 60 * 1000UL;

  uint32_t start = millis();
  bool exitRequested = false;
  for (int i = 0; !exitRequested && millis() - start < kMaxMs; i = (i + 1) % kPhases) {
    const Phase &p = phases[i];
    if (p.pwm) {
      ledcAttachPin(PIN_LCD_LED, kBacklightChannel);
      writeBacklight(p.value);
    } else {
      ledcDetachPin(PIN_LCD_LED);
      pinMode(PIN_LCD_LED, OUTPUT);
      digitalWrite(PIN_LCD_LED, p.value);
    }
    Serial.printf("bltest: %s\n", p.name);

    lcd.clearBuffer();
    lcd.setFont(u8g2_font_6x10_tf);
    drawCentered("BACKLIGHT TEST", 10);
    lcd.setFont(u8g2_font_logisoso16_tr);
    drawCentered(p.name, 40);
    lcd.setFont(u8g2_font_5x8_tf);
    drawCentered("IO21   push = exit", 62);
    lcd.sendBuffer();

    uint32_t phaseStart = millis();
    while (!exitRequested && millis() - phaseStart < kPhaseMs) {
      exitRequested = digitalRead(PIN_JOG_PUSH) == LOW;
      delay(10);
    }
  }

  ledcAttachPin(PIN_LCD_LED, kBacklightChannel);
  setBacklightTarget(!dimmed);
  snapBacklight();
  redraw = true;
  Serial.println("bltest: done");
}

const uint8_t *Ui::frameBuffer() { return lcd.getBufferPtr(); }

bool Ui::frameFlipped() { return !Settings::get(S_ROTATE); }  // U8G2_R2 unless "Rotate 180" is on

void Ui::setScreen(int index) {
  Menu::close();
  screen = constrain(index, 0, ScrCount - 1);
  showStats = false;
  redraw = true;
}
