#include "export.h"

#include <TinyGPSPlus.h>
#include <math.h>
#include <time.h>

#include <algorithm>

#include "logger.h"
#include "settings.h"
#include "version.h"

const char *const Export::kCsvHeader = "time,latitude,longitude,elevation,speed_kmh,course,hdop,sats,fix,battery_v";

namespace {

constexpr int kColorBuckets = 12;
constexpr int kSpeedBins = 400;      // 1 km/h histogram bins for the color scale
constexpr uint32_t kGapBreakS = 60;  // don't draw a line across logging gaps longer than this
constexpr const char *kLineEnd = "</coordinates></LineString></Placemark>\n";

void formatTime(uint32_t epoch, bool local, const char *fmt, char *out, size_t n) {
  time_t t = epoch;
  tm parts;
  if (local) {
    localtime_r(&t, &parts);
  } else {
    gmtime_r(&t, &parts);
  }
  strftime(out, n, fmt, &parts);
}

double speedKmh(const LogRecord &r) { return r.speedCms * 0.036; }

bool readRecord(File &f, LogRecord &r) { return f.read((uint8_t *)&r, sizeof r) == sizeof r; }

// Same palette as the ESP8266 logger: hue 0 (red, stopped) .. 300 (magenta, fastest)
void bucketColor(int bucket, char *kmlColor, char *htmlColor) {
  float h = 300.0f * bucket / (kColorBuckets - 1) / 60.0f;
  constexpr float v = 0.9f;
  float x = v * (1 - fabsf(fmodf(h, 2.0f) - 1));
  float r = 0, g = 0, b = 0;
  switch ((int)h) {
    case 0: r = v; g = x; break;
    case 1: r = x; g = v; break;
    case 2: g = v; b = x; break;
    case 3: g = x; b = v; break;
    case 4: r = x; b = v; break;
    default: r = v; b = x; break;
  }
  uint8_t R = r * 255 + 0.5f, G = g * 255 + 0.5f, B = b * 255 + 0.5f;
  if (kmlColor) snprintf(kmlColor, 9, "ff%02x%02x%02x", B, G, R);  // KML order is aabbggrr
  if (htmlColor) snprintf(htmlColor, 7, "%02x%02x%02x", R, G, B);
}

int bucketFor(double kmh, double vmax) {
  int b = (int)lround(kmh / vmax * (kColorBuckets - 1));
  return constrain(b, 0, kColorBuckets - 1);
}

void writeCoord(Print &out, const LogRecord &r) {
  out.printf("%.6f,%.6f,%.1f\n", r.lonE7 / 1e7, r.latE7 / 1e7, r.altCm / 100.0);
}

void writeMarker(Print &out, const char *name, const char *style, uint32_t epoch, double lat, double lon) {
  char when[32];
  formatTime(epoch, true, "%d.%m.%Y %H:%M:%S", when, sizeof when);
  out.printf("<Placemark><name>%s</name><description>%s</description><styleUrl>#%s</styleUrl>"
             "<Point><coordinates>%.6f,%.6f</coordinates></Point></Placemark>\n",
             name, when, style, lon, lat);
}

}  // namespace

TrackSummary Export::summarize(File &f) {
  const double movingKmh = Settings::real(S_GPS_MOVING);
  TrackSummary s;
  uint32_t hist[kSpeedBins] = {};
  LogRecord r;
  bool havePrev = false;
  double prevLat = 0, prevLon = 0;
  uint32_t prevEpoch = 0;

  while (readRecord(f, r)) {
    if (!(r.flags & kLogFlagFix)) continue;
    double lat = r.latE7 / 1e7, lon = r.lonE7 / 1e7, alt = r.altCm / 100.0, kmh = speedKmh(r);
    if (s.points == 0) {
      s.firstEpoch = r.epoch;
      s.startLat = lat;
      s.startLon = lon;
      s.minAlt = s.maxAlt = alt;
    }
    s.points++;
    s.lastEpoch = r.epoch;
    s.endLat = lat;
    s.endLon = lon;
    s.minAlt = std::min(s.minAlt, alt);
    s.maxAlt = std::max(s.maxAlt, alt);
    s.maxKmh = std::max(s.maxKmh, kmh);
    hist[std::min((int)kmh, kSpeedBins - 1)]++;

    if (havePrev && r.epoch - prevEpoch <= kGapBreakS && kmh >= movingKmh) {
      s.distanceM += TinyGPSPlus::distanceBetween(prevLat, prevLon, lat, lon);
      s.movingS += r.epoch - prevEpoch;
    }
    prevLat = lat;
    prevLon = lon;
    prevEpoch = r.epoch;
    havePrev = true;
  }

  uint32_t target = s.points * 98 / 100, seen = 0;
  for (int i = 0; i < kSpeedBins && s.points; i++) {
    seen += hist[i];
    if (seen > target) {
      s.colorMaxKmh = std::max(i + 1, 5);  // at least 5 km/h so a slow walk isn't all magenta
      break;
    }
  }
  return s;
}

uint32_t Export::csv(File &f, Print &out) {
  LogRecord r;
  char ts[24];
  char hdop[8];
  char line[160];
  uint32_t n = 0;
  while (readRecord(f, r)) {
    formatTime(r.epoch, false, "%Y-%m-%d %H:%M:%S", ts, sizeof ts);
    if (r.hdopC == 0xFFFF) {
      hdop[0] = '\0';
    } else {
      snprintf(hdop, sizeof hdop, "%.2f", r.hdopC / 100.0);
    }
    if (r.flags & kLogFlagFix) {
      snprintf(line, sizeof line, "%s,%.7f,%.7f,%.1f,%.1f,%.1f,%s,%u,1,%.3f\n", ts, r.latE7 / 1e7, r.lonE7 / 1e7,
               r.altCm / 100.0, speedKmh(r), r.courseCdeg / 100.0, hdop, (unsigned)r.sats, r.batteryMv / 1000.0);
    } else {
      snprintf(line, sizeof line, "%s,,,,,,%s,%u,0,%.3f\n", ts, hdop, (unsigned)r.sats, r.batteryMv / 1000.0);
    }
    out.print(line);
    if (++n % 256 == 0) delay(1);
  }
  return n;
}

void Export::kml(File &f, const String &name, Print &out) {
  TrackSummary s = summarize(f);
  f.seek(kLogHeaderSize);

  out.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n");
  out.printf("<name>GPS_%s</name>\n<open>1</open>\n", name.c_str());

  if (s.points) {
    char from[32], to[16];
    formatTime(s.firstEpoch, true, "%d.%m.%Y %H:%M", from, sizeof from);
    formatTime(s.lastEpoch, true, "%H:%M", to, sizeof to);
    uint32_t total = s.lastEpoch - s.firstEpoch;
    out.printf("<description><![CDATA[%s - %s<br>Distance %.2f km<br>Duration %u:%02u:%02u, moving %u:%02u:%02u<br>"
               "Max %.1f km/h, moving average %.1f km/h<br>Altitude %.0f - %.0f m<br>%u points]]></description>\n",
               from, to, s.distanceM / 1000.0, (unsigned)(total / 3600), (unsigned)(total / 60 % 60),
               (unsigned)(total % 60), (unsigned)(s.movingS / 3600), (unsigned)(s.movingS / 60 % 60),
               (unsigned)(s.movingS % 60), s.maxKmh, s.movingS ? s.distanceM / s.movingS * 3.6 : 0.0, s.minAlt,
               s.maxAlt, (unsigned)s.points);
  }

  // Shared styles keep the file small: one per color bucket instead of one per segment
  char kmlColor[9], htmlColor[7];
  for (int i = 0; i < kColorBuckets; i++) {
    bucketColor(i, kmlColor, nullptr);
    out.printf("<Style id=\"s%d\"><LineStyle><color>%s</color><width>4</width></LineStyle></Style>\n", i, kmlColor);
  }
  out.print("<Style id=\"start\"><IconStyle><Icon><href>http://maps.google.com/mapfiles/kml/paddle/grn-circle.png"
            "</href></Icon></IconStyle></Style>\n"
            "<Style id=\"end\"><IconStyle><Icon><href>http://maps.google.com/mapfiles/kml/paddle/red-square.png"
            "</href></Icon></IconStyle></Style>\n");

  out.print("<Folder><name>Legend: speed (km/h)</name>\n");
  for (int k = 4; k >= 0; k--) {
    int bucket = (kColorBuckets - 1) * k / 4;
    bucketColor(bucket, nullptr, htmlColor);
    out.printf("<Placemark><name><![CDATA[<span style=\"color:#%s\"><b>%.0f%s</b></span>]]></name></Placemark>\n",
               htmlColor, s.colorMaxKmh * bucket / (kColorBuckets - 1), k == 4 ? "+" : "");
  }
  out.print("</Folder>\n<Folder><name>Track</name>\n");

  // Consecutive points in the same color bucket share one LineString
  LogRecord r, prev;
  bool havePrev = false;
  bool inLine = false;
  int lineBucket = 0;
  uint32_t n = 0;
  while (readRecord(f, r)) {
    if (!(r.flags & kLogFlagFix)) continue;
    if (havePrev && r.epoch - prev.epoch > kGapBreakS) {
      if (inLine) out.print(kLineEnd);
      inLine = false;
      havePrev = false;
    }
    if (havePrev) {
      int bucket = bucketFor(0.5 * (speedKmh(prev) + speedKmh(r)), s.colorMaxKmh);
      if (inLine && bucket != lineBucket) {
        out.print(kLineEnd);
        inLine = false;
      }
      if (!inLine) {
        out.printf("<Placemark><styleUrl>#s%d</styleUrl><LineString><tessellate>1</tessellate><coordinates>\n", bucket);
        writeCoord(out, prev);
        inLine = true;
        lineBucket = bucket;
      }
      writeCoord(out, r);
    }
    prev = r;
    havePrev = true;
    if (++n % 256 == 0) delay(1);
  }
  if (inLine) out.print(kLineEnd);

  if (s.points) {
    writeMarker(out, "Start", "start", s.firstEpoch, s.startLat, s.startLon);
    writeMarker(out, "End", "end", s.lastEpoch, s.endLat, s.endLon);
  }
  out.print("</Folder>\n</Document>\n</kml>\n");
}

void Export::gpx(File &f, const String &name, Print &out) {
  out.print("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"" FW_NAME " " FW_VERSION
            "\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n");
  out.printf("<trk><name>GPS_%s</name>\n<trkseg>\n", name.c_str());
  LogRecord r;
  bool havePrev = false;
  uint32_t prevEpoch = 0;
  char ts[24];
  uint32_t n = 0;
  while (readRecord(f, r)) {
    if (!(r.flags & kLogFlagFix)) continue;
    if (havePrev && r.epoch - prevEpoch > kGapBreakS) out.print("</trkseg>\n<trkseg>\n");
    formatTime(r.epoch, false, "%Y-%m-%dT%H:%M:%SZ", ts, sizeof ts);
    out.printf("<trkpt lat=\"%.7f\" lon=\"%.7f\"><ele>%.1f</ele><time>%s</time><sat>%u</sat>", r.latE7 / 1e7,
               r.lonE7 / 1e7, r.altCm / 100.0, ts, (unsigned)r.sats);
    if (r.hdopC != 0xFFFF) out.printf("<hdop>%.2f</hdop>", r.hdopC / 100.0);
    out.print("</trkpt>\n");
    prevEpoch = r.epoch;
    havePrev = true;
    if (++n % 256 == 0) delay(1);
  }
  out.print("</trkseg></trk>\n</gpx>\n");
}

void Export::trackJson(File &f, const String &name, size_t maxPoints, Print &out) {
  TrackSummary s = summarize(f);
  f.seek(kLogHeaderSize);
  uint32_t stride = maxPoints && s.points > maxPoints ? (s.points + maxPoints - 1) / maxPoints : 1;

  out.printf("{\"name\":\"%s\",\"summary\":{\"points\":%u,\"start\":%u,\"end\":%u,\"distance\":%.1f,\"moving\":%u,"
             "\"maxKmh\":%.2f,\"avgKmh\":%.2f,\"colorMaxKmh\":%.1f,\"minAlt\":%.1f,\"maxAlt\":%.1f},\"points\":[",
             name.c_str(), (unsigned)s.points, (unsigned)s.firstEpoch, (unsigned)s.lastEpoch, s.distanceM,
             (unsigned)s.movingS, s.maxKmh, s.movingS ? s.distanceM / s.movingS * 3.6 : 0.0, s.colorMaxKmh,
             s.points ? s.minAlt : 0.0, s.points ? s.maxAlt : 0.0);

  LogRecord r;
  uint32_t index = 0;
  bool first = true;
  while (readRecord(f, r)) {
    if (!(r.flags & kLogFlagFix)) continue;
    if (index++ % stride) continue;
    out.printf("%s[%.6f,%.6f,%.1f,%.0f,%u]", first ? "" : ",", r.latE7 / 1e7, r.lonE7 / 1e7, speedKmh(r),
               r.altCm / 100.0, (unsigned)(r.epoch - s.firstEpoch));
    first = false;
    if (index % 256 == 0) delay(1);
  }
  out.print("]}");
}
