#include "trip.h"

#include <Arduino.h>

#include "gps.h"
#include "settings.h"

namespace {

TripStats trip;
bool havePrev = false;
double prevLat = 0;
double prevLon = 0;
uint32_t prevMs = 0;
bool haveAltRef = false;
double altRef = 0;

}  // namespace

void Trip::update() {
  // Below the moving threshold position changes are GPS jitter; small altitude changes are noise
  const double kMovingKmh = Settings::real(S_GPS_MOVING);
  const double kMaxHdop = Settings::real(S_GPS_MAXHDOP);
  const double kAltHysteresisM = Settings::get(S_GPS_ALTHYST);

  TinyGPSPlus &g = Gps::raw();
  if (!Gps::hasFix() || g.satellites.value() < 4 || !g.hdop.isValid() || g.hdop.hdop() > kMaxHdop) return;

  double lat = g.location.lat();
  double lon = g.location.lng();
  double kmh = g.speed.kmph();
  uint32_t now = millis();

  if (havePrev && kmh >= kMovingKmh) {
    trip.distanceM += TinyGPSPlus::distanceBetween(prevLat, prevLon, lat, lon);
    trip.movingMs += now - prevMs;
    trip.speedKmh.add(kmh);
  }
  prevLat = lat;
  prevLon = lon;
  prevMs = now;
  havePrev = true;

  if (!g.altitude.isValid()) return;
  double alt = g.altitude.meters();
  trip.altitudeM.add(alt);
  if (!haveAltRef) {
    altRef = alt;
    haveAltRef = true;
  } else if (alt > altRef + kAltHysteresisM) {
    trip.climbM += alt - altRef;
    altRef = alt;
  } else if (alt < altRef - kAltHysteresisM) {
    trip.descentM += altRef - alt;
    altRef = alt;
  }
}

void Trip::reset() {
  trip = TripStats();
  havePrev = false;
  haveAltRef = false;
}

const TripStats &Trip::get() { return trip; }
