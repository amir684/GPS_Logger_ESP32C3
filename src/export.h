#pragma once

#include <Arduino.h>
#include <FS.h>

struct TrackSummary {
  uint32_t points = 0;  // records with a fix
  uint32_t firstEpoch = 0;
  uint32_t lastEpoch = 0;
  double startLat = 0, startLon = 0;
  double endLat = 0, endLon = 0;
  double distanceM = 0;
  uint32_t movingS = 0;
  double maxKmh = 0;
  double colorMaxKmh = 5;  // 98th percentile, so a single GPS spike doesn't wash out the colors
  double minAlt = 0, maxAlt = 0;
};

// Session file -> text formats. All expect the File positioned at the first record (Logger::openSession).
namespace Export {
extern const char *const kCsvHeader;  // GPS Visualizer compatible column names
TrackSummary summarize(File &session);  // reads to the end of the file
uint32_t csv(File &session, Print &out);  // rows only, returns row count
void kml(File &session, const String &name, Print &out);  // speed-colored track for Google Earth / My Maps
void gpx(File &session, const String &name, Print &out);  // GPX 1.1 for Strava, OsmAnd, Garmin...
// Summary plus at most maxPoints [lat, lon, km/h, alt m, seconds from start] for the web preview
void trackJson(File &session, const String &name, size_t maxPoints, Print &out);
}  // namespace Export
