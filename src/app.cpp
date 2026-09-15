#include "app.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "autolog.h"
#include "battery.h"
#include "config.h"
#include "gps.h"
#include "logger.h"
#include "net.h"
#include "settings.h"
#include "trip.h"
#include "ui.h"
#include "units.h"

namespace {

constexpr uint32_t kAutoSleepGraceMs = 30000;  // let the battery filter settle after boot

bool restartPending = false;
uint32_t restartAtMs = 0;
bool sleepPending = false;
uint32_t sleepAtMs = 0;
int lastWifiMode = WM_AP;
float cachedTemp = NAN;
uint32_t tempReadMs = 0;

void enterDeepSleep() {
  Ui::showMessage("Sleeping", "push the wheel to wake");
  Logger::close();
  Settings::saveNow();
  Net::stop();
  delay(800);

  // A held button would wake the chip straight away
  uint32_t start = millis();
  while (digitalRead(PIN_JOG_PUSH) == LOW && millis() - start < 5000) delay(10);

  Ui::shutdown();
  ledcDetachPin(PIN_LCD_LED);
  pinMode(PIN_LCD_LED, OUTPUT);
  digitalWrite(PIN_LCD_LED, LCD_LED_ACTIVE_LOW ? HIGH : LOW);
  gpio_hold_en((gpio_num_t)PIN_LCD_LED);
  gpio_deep_sleep_hold_en();

  gpio_pullup_en((gpio_num_t)PIN_JOG_PUSH);
  gpio_pulldown_dis((gpio_num_t)PIN_JOG_PUSH);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_JOG_PUSH, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

}  // namespace

void App::loop() {
  uint32_t now = millis();
  if (restartPending && now >= restartAtMs) {
    Ui::showMessage("Restarting", "");
    Logger::close();
    Settings::saveNow();
    delay(300);
    ESP.restart();
  }
  if (sleepPending && now >= sleepAtMs) {
    sleepPending = false;
    enterDeepSleep();
  }

  int32_t sleepMv = Settings::value(S_BAT_SLEEP);
  if (sleepMv && !sleepPending && now > kAutoSleepGraceMs && Battery::present() && Battery::volts() * 1000 < sleepMv) {
    Ui::toast("Battery empty");
    sleep(3000);
  }
}

void App::applySetting(int id) {
  switch (id) {
    case S_LOGGING:
      if (!Settings::get(S_LOGGING)) Logger::close();
      break;
    case S_LOG_FULL:
      Logger::setStopWhenFull(Settings::get(S_LOG_FULL) == 1);
      break;
    case S_CONTRAST:
    case S_BL_LEVEL:
    case S_BL_IDLE:
    case S_ROTATE:
    case S_INVERT:
      Ui::applyDisplay();
      break;
    case S_TZ:
      Units::applyTimezone();
      break;
    case S_GPS_BAUD:
      Gps::setBaud(Settings::value(S_GPS_BAUD));
      break;
    case S_CPU_MHZ:
      setCpuFrequencyMhz(Settings::value(S_CPU_MHZ));
      break;
    case S_WIFI_MODE:
      Net::apply(1500);  // give a web request time to get its answer before WiFi restarts
      break;
    case S_AP_SSID:
    case S_AP_PASS:
    case S_STA_SSID:
    case S_STA_PASS:
    case S_HOSTNAME:
    case S_WIFI_TXPWR:
      if (Net::active()) Net::apply(1500);
      break;
    default:
      break;
  }
}

void App::setLogging(bool on) {
  if (Settings::set(S_LOGGING, on)) applySetting(S_LOGGING);
  AutoLog::noteManual(on);  // a manual choice wins over the movement detector
  Ui::toast(on ? "Logging ON" : "Logging OFF");
}

void App::newSession() {
  Logger::close();
  Ui::toast("New session");
}

void App::resetTrip() {
  Trip::reset();
  Ui::toast("Trip reset");
}

void App::eraseLogs() {
  Logger::removeAll();
  Ui::toast("Logs erased");
}

void App::factoryReset() {
  Settings::factoryReset();
  Ui::showMessage("Factory reset", "restarting...");
  restart(1500);
}

void App::restart(uint32_t delayMs) {
  restartPending = true;
  restartAtMs = millis() + delayMs;
}

void App::sleep(uint32_t delayMs) {
  sleepPending = true;
  sleepAtMs = millis() + delayMs;
}

void App::setWifi(bool on) {
  int mode = Settings::get(S_WIFI_MODE);
  if (on) {
    Settings::set(S_WIFI_MODE, mode != WM_OFF ? mode : lastWifiMode);
    Net::apply(0);
    Ui::toast("WiFi ON");
  } else {
    if (mode != WM_OFF) lastWifiMode = mode;
    Settings::set(S_WIFI_MODE, WM_OFF);
    Net::apply(300);
    Ui::toast("WiFi OFF");
  }
}

const char *App::resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brown-out";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_UNKNOWN: return "USB/unknown";
    default: {
      static char code[16];
      snprintf(code, sizeof code, "code %d", (int)esp_reset_reason());  // e.g. USB-JTAG reset after flashing
      return code;
    }
  }
}

bool App::wokeFromSleep() { return esp_reset_reason() == ESP_RST_DEEPSLEEP; }

float App::chipTemp() {
  if (isnan(cachedTemp) || millis() - tempReadMs > 5000) {
    cachedTemp = temperatureRead();
    tempReadMs = millis();
  }
  return cachedTemp;
}
