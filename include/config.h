#pragma once

#include <cstdint>

// ST7567 LCD (hardware SPI)
constexpr int PIN_LCD_SCK = 6;
constexpr int PIN_LCD_SDI = 7;
constexpr int PIN_LCD_CS = 10;
constexpr int PIN_LCD_DC = 5;
constexpr int PIN_LCD_RST = 9;
constexpr int PIN_LCD_LED = 21;  // TXD pad -> 100R -> gate of an AO3400A that switches the LED cathode to GND
constexpr bool LCD_LED_ACTIVE_LOW = false;  // true when the LED wire is driven directly by the pin (no transistor)

// Jog switch: common to GND, internal pull-ups. Swap UP/DOWN if the wheel feels reversed.
constexpr int PIN_JOG_UP = 1;
constexpr int PIN_JOG_PUSH = 3;  // GPIO0-5 can wake the chip from deep sleep
constexpr int PIN_JOG_DOWN = 4;

// Battery: BAT+ -> 100k -> IO0 -> 100k -> GND, plus 100nF from IO0 to GND
constexpr int PIN_BAT_ADC = 0;  // divider ratio is the "bat_cal" setting

// GPS NMEA output (baud rate is the "gps_baud" setting)
constexpr int PIN_GPS_RX = 20;  // ESP RX <- GPS TX

// GPS power switch: P-MOSFET high side on the 3V3 rail, gate on IO8 with a 100k pull-up.
// LOW = GPS powered, HIGH or floating = GPS off. The module's backup cell stays connected,
// so waking up is a hot start.
constexpr int PIN_GPS_POWER = 8;
constexpr bool GPS_POWER_ACTIVE_LOW = true;

// IO2 is the only spare pin left. It is a strapping pin: whatever you connect there
// must not pull it low while the chip boots.
