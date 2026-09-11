#pragma once

#include <cstdint>

// ST7567 LCD (hardware SPI)
constexpr int PIN_LCD_SCK = 6;
constexpr int PIN_LCD_SDI = 7;
constexpr int PIN_LCD_CS = 10;
constexpr int PIN_LCD_DC = 5;
constexpr int PIN_LCD_RST = 9;
constexpr int PIN_LCD_LED = 21;  // TXD pad; the LED anode sits on 3.3V, so the pin sinks current
constexpr bool LCD_LED_ACTIVE_LOW = true;

// Jog switch: common to GND, internal pull-ups. Swap UP/DOWN if the wheel feels reversed.
constexpr int PIN_JOG_UP = 1;
constexpr int PIN_JOG_PUSH = 3;  // GPIO0-5 can wake the chip from deep sleep
constexpr int PIN_JOG_DOWN = 4;

// Battery: BAT+ -> 100k -> IO0 -> 100k -> GND, plus 100nF from IO0 to GND
constexpr int PIN_BAT_ADC = 0;  // divider ratio is the "bat_cal" setting

// GPS NMEA output (baud rate is the "gps_baud" setting)
constexpr int PIN_GPS_RX = 20;  // ESP RX <- GPS TX

// I2C for future sensors; the pull-ups also hold strapping pins IO2/IO8 high at boot
constexpr int PIN_I2C_SDA = 2;
constexpr int PIN_I2C_SCL = 8;
