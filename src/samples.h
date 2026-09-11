#pragma once

#include <cstdint>

#include "history.h"

constexpr size_t kHistorySize = 100;
constexpr uint32_t kBatteryGraphPeriodS = 60;

// Speed and altitude are sampled at the log interval, battery every kBatteryGraphPeriodS
extern History<kHistorySize> histSpeed;
extern History<kHistorySize> histAlt;
extern History<kHistorySize> histBattery;
