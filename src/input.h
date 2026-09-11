#pragma once

#include <cstdint>

enum class Key : uint8_t { None, Up, Down, Push, LongPush, SleepHold };

constexpr uint32_t kLongPressMs = 800;   // push released after this = LongPush
constexpr uint32_t kSleepHoldMs = 3000;  // push still held after this = SleepHold

namespace Input {
void begin();
Key poll();  // at most one event per call; Up/Down auto-repeat while held
uint32_t lastActivityMs();
uint32_t pushHoldMs();  // how long the push button has been held, 0 when released or already handled
void inject(Key key);  // simulated press from the serial console or the web remote
}  // namespace Input
