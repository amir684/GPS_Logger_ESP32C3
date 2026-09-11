#pragma once

namespace Battery {
void begin();
void update();  // call every loop; samples once per second
float volts();
int percent();
bool present();         // a cell is connected (reading above 2.5 V); false when running from USB only
bool low();             // below the warning threshold
int runtimeMinutes();   // estimate from capacity and average current, -1 without a cell
}  // namespace Battery
