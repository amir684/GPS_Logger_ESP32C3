#pragma once

#include <Arduino.h>

// WiFi manager driven by the WiFi settings: access point, home network, mDNS, idle auto-off
namespace Net {
void begin();                     // start according to the wifi_mode setting
void update();                    // home WiFi fallback, idle auto-off, delayed re-apply
void apply(uint32_t delayMs = 0);  // restart WiFi with current settings (delay lets a web reply go out first)
void stop();

bool active();
bool apActive();
bool staConnected();
String apIp();
String staIp();
String primaryIp();  // home network address when connected, otherwise the AP address
int clients();       // stations on the access point
int rssi();          // home network signal, 0 when not connected
const char *modeText();
}  // namespace Net
