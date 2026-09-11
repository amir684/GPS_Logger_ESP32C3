#include "input.h"

#include <Arduino.h>

#include "config.h"

namespace {

constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kRepeatDelayMs = 450;
constexpr uint32_t kRepeatMs = 150;

struct Button {
  int pin;
  Key key;
  bool raw;
  bool pressed;
  bool longFired;
  uint32_t changedMs;
  uint32_t pressedMs;
  uint32_t repeatAtMs;
};

Button buttons[] = {
    {PIN_JOG_UP, Key::Up},
    {PIN_JOG_DOWN, Key::Down},
    {PIN_JOG_PUSH, Key::Push},
};

uint32_t lastActivity = 0;

constexpr int kQueueSize = 8;
Key injected[kQueueSize];
int injectedHead = 0;
int injectedCount = 0;

}  // namespace

void Input::begin() {
  for (Button &b : buttons) pinMode(b.pin, INPUT_PULLUP);
}

Key Input::poll() {
  uint32_t now = millis();
  if (injectedCount) {
    Key key = injected[injectedHead];
    injectedHead = (injectedHead + 1) % kQueueSize;
    injectedCount--;
    lastActivity = now;
    return key;
  }
  for (Button &b : buttons) {
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.raw) {
      b.raw = raw;
      b.changedMs = now;
    }

    if (raw != b.pressed && now - b.changedMs >= kDebounceMs) {
      b.pressed = raw;
      lastActivity = now;
      if (raw) {
        b.pressedMs = now;
        b.repeatAtMs = now + kRepeatDelayMs;
        b.longFired = false;
        if (b.key != Key::Push) return b.key;
      } else if (b.key == Key::Push && !b.longFired) {
        // Push acts on release, so a long hold can still turn into SleepHold
        return now - b.pressedMs >= kLongPressMs ? Key::LongPush : Key::Push;
      }
    }

    if (!b.pressed) continue;
    if (b.key == Key::Push) {
      if (!b.longFired && now - b.pressedMs >= kSleepHoldMs) {
        b.longFired = true;
        lastActivity = now;
        return Key::SleepHold;
      }
    } else if (now >= b.repeatAtMs) {
      b.repeatAtMs = now + kRepeatMs;
      lastActivity = now;
      return b.key;
    }
  }
  return Key::None;
}

uint32_t Input::lastActivityMs() { return lastActivity; }

uint32_t Input::pushHoldMs() {
  const Button &push = buttons[2];
  return push.pressed && !push.longFired ? millis() - push.pressedMs : 0;
}

void Input::inject(Key key) {
  if (key == Key::None || injectedCount == kQueueSize) return;
  injected[(injectedHead + injectedCount) % kQueueSize] = key;
  injectedCount++;
}
