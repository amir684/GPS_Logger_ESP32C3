#pragma once

#include <cstdint>

#include "stats.h"

struct TripStats {
  double distanceM = 0;
  uint32_t movingMs = 0;
  double climbM = 0;
  double descentM = 0;
  RunningStats speedKmh;  // only samples taken while moving
  RunningStats altitudeM;
};

namespace Trip {
void update();  // call once per GPS epoch
void reset();
const TripStats &get();
}  // namespace Trip
