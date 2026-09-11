#pragma once

#include <time.h>

#include <cstddef>

// Conversions and formatting that follow the Units & time settings
namespace Units {
double speed(double kmh);
const char *speedUnit();
double distance(double meters);  // km, mi or nm
const char *distanceUnit();
double altitude(double meters);  // m or ft
const char *altitudeUnit();

void applyTimezone();
const char *timezonePosix(int index);
void formatDate(const tm &t, char *out, size_t n);
void formatTime(const tm &t, bool seconds, char *out, size_t n);  // honours the 12-hour setting
const char *cardinal(double degrees);                              // 16-point compass name
}  // namespace Units
