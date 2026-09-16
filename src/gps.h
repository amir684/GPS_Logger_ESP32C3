#pragma once

#include <TinyGPSPlus.h>

namespace Gps {
void begin();
void update();  // feed NMEA and keep the system clock synced to GPS time
void setBaud(uint32_t baud);
void setPower(bool on);  // high-side switch on the GPS 3V3 rail
bool powered();
TinyGPSPlus &raw();
bool hasFix();     // valid location within the last 3 s
bool timeValid();  // system clock has been set from GPS
}  // namespace Gps
