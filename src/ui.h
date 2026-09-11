#pragma once

#include <cstdint>

namespace Ui {
void begin();
void update();  // jog input, menu, backlight timeout, screen drawing

void applyDisplay();  // contrast, rotation, invert and backlight from settings
void toast(const char *text);
void showMessage(const char *title, const char *detail);  // full-screen message, sent immediately
void shutdown();  // display and backlight off before deep sleep

// Screen mirror for the console and the web page. 128x64 frame buffer in U8g2 layout:
// pixel (x, y) is bit (y % 8) of byte (y / 8) * 128 + x, in controller orientation.
const uint8_t *frameBuffer();
bool frameFlipped();  // true when the buffer is rotated 180 degrees relative to the viewer
void setScreen(int index);

// Hardware bring-up helpers for the serial console
void testPattern(int mode);  // 0 = all pixels off, 1 = all on, 2 = frame + checkerboard; held until a key press
void reinit();
void backlightTest();  // blocking: cycles GPIO/PWM modes on the LED pin until push or 5 min
}  // namespace Ui
