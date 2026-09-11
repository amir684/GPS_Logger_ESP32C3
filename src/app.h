#pragma once

#include <Arduino.h>

// Actions shared by the device menu, the web page and the serial console
namespace App {
void loop();  // runs delayed restart / sleep requests

void applySetting(int id);  // push a changed setting into the running modules
void setLogging(bool on);
void newSession();
void resetTrip();
void eraseLogs();
void factoryReset();  // restore default settings and restart
void restart(uint32_t delayMs = 800);
void sleep(uint32_t delayMs = 800);  // deep sleep, wake with the jog push button
void setWifi(bool on);

const char *resetReason();
bool wokeFromSleep();
float chipTemp();  // cached, refreshed every few seconds
}  // namespace App
