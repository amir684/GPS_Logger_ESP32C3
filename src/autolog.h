#pragma once

#include <Arduino.h>

// Starts and stops logging sessions by movement, using the Auto logging settings.
// Hysteresis (start speed above stop speed) plus a delay in each direction keeps GPS noise,
// traffic lights and short stops from opening and closing sessions by accident.
namespace AutoLog {
void update();             // call every loop
void noteManual(bool on);  // a manual start/stop overrides the detector until the vehicle stops
bool armed();              // enabled and waiting for movement
uint32_t stopCountdownS();  // seconds left before an automatic stop, 0 when not counting down
}  // namespace AutoLog
