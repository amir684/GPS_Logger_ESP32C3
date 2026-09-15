#include "web.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "app.h"
#include "autolog.h"
#include "battery.h"
#include "export.h"
#include "gps.h"
#include "input.h"
#include "logger.h"
#include "net.h"
#include "samples.h"
#include "settings.h"
#include "trip.h"
#include "ui.h"
#include "units.h"
#include "version.h"
#include "web_page_html.h"
#include "web_page_js.h"

namespace {

WebServer server(80);
bool running = false;
bool routesAdded = false;
uint32_t lastRequest = 0;
bool otaOk = false;
bool otaAuthorized = false;
bool debug = false;

void logRequest(const char *note) {
  if (!debug) return;
  Serial.printf("[web] %s http://%s%s from %s%s\n", server.method() == HTTP_GET ? "GET" : "POST",
                server.hostHeader().c_str(), server.uri().c_str(), server.client().remoteIP().toString().c_str(), note);
}

// Buffers output into HTTP chunks and keeps the GPS parser fed while a long export streams
class ChunkedPrint : public Print {
 public:
  using Print::write;
  size_t write(uint8_t c) override {
    buf_[len_++] = c;
    if (len_ == sizeof buf_) send();
    return 1;
  }
  size_t write(const uint8_t *data, size_t size) override {
    for (size_t i = 0; i < size; i++) write(data[i]);
    return size;
  }
  void send() {
    if (!len_) return;
    server.sendContent((const char *)buf_, len_);
    len_ = 0;
    Gps::update();
  }

 private:
  uint8_t buf_[1024];
  size_t len_ = 0;
};

bool passwordOk() {
  const String &pass = Settings::text(S_WEB_PASS);
  return pass.isEmpty() || server.authenticate("admin", pass.c_str());
}

// Every handler starts here: activity tracking for WiFi auto-off and optional basic auth
bool accept() {
  lastRequest = millis();
  if (lastRequest == 0) lastRequest = 1;
  logRequest("");
  if (passwordOk()) return true;
  server.requestAuthentication(BASIC_AUTH, FW_NAME);
  return false;
}

void sendJson(const JsonDocument &doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const char *message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(doc, code);
}

String sessionArg() {
  String name = server.arg("f");
  return name == "last" ? Logger::latestSession() : name;
}

void beginStream(const char *contentType, const String &filename) {
  if (filename.length()) server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);  // chunked: generated while streaming
  server.send(200, contentType, "");
}

void endStream(ChunkedPrint &out) {
  out.send();
  server.sendContent("");
}

// ---------------------------------------------------------------- handlers

void handleIndex() {
  if (!accept()) return;
  server.sendHeader("Cache-Control", "no-cache");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(kPageHtml);
  server.sendContent_P(kPageJs);
  server.sendContent("");
}

void handleStatus() {
  if (!accept()) return;
  static uint32_t sketchSize = 0;
  static uint32_t sketchFree = 0;
  if (!sketchSize) {
    sketchSize = ESP.getSketchSize();
    sketchFree = ESP.getFreeSketchSpace();
  }

  TinyGPSPlus &g = Gps::raw();
  const TripStats &t = Trip::get();
  JsonDocument doc;
  doc["name"] = Settings::text(S_DEV_NAME);
  doc["fw"] = FW_VERSION;
  doc["uptime"] = millis() / 1000;
  doc["time"] = Gps::timeValid() ? (uint32_t)time(nullptr) : 0;
  if (Gps::timeValid()) {
    time_t now = time(nullptr);
    tm lt;
    localtime_r(&now, &lt);
    char date[20], clock[12];
    Units::formatDate(lt, date, sizeof date);
    Units::formatTime(lt, true, clock, sizeof clock);
    doc["local"] = String(date) + " " + clock;
  }

  JsonObject gps = doc["gps"].to<JsonObject>();
  gps["fix"] = Gps::hasFix();
  gps["timeValid"] = Gps::timeValid();
  gps["sats"] = g.satellites.value();
  gps["hdop"] = g.hdop.hdop();
  gps["lat"] = g.location.lat();
  gps["lon"] = g.location.lng();
  gps["alt"] = g.altitude.meters();
  gps["speed"] = g.speed.kmph();
  gps["course"] = g.course.deg();
  gps["age"] = g.location.isValid() ? (long)g.location.age() : -1L;  // -1 would otherwise wrap to 4294967295
  gps["chars"] = g.charsProcessed();
  gps["passed"] = g.passedChecksum();
  gps["failed"] = g.failedChecksum();

  JsonObject trip = doc["trip"].to<JsonObject>();
  trip["distance"] = t.distanceM;
  trip["moving"] = t.movingMs / 1000;
  trip["avg"] = t.speedKmh.mean();
  trip["max"] = t.speedKmh.maxVal();
  trip["sd"] = t.speedKmh.stddev();
  trip["climb"] = t.climbM;
  trip["descent"] = t.descentM;
  trip["altMin"] = t.altitudeM.minVal();
  trip["altMax"] = t.altitudeM.maxVal();
  trip["samples"] = t.speedKmh.count();

  JsonObject battery = doc["battery"].to<JsonObject>();
  battery["v"] = Battery::volts();
  battery["pct"] = Battery::percent();
  battery["present"] = Battery::present();
  battery["low"] = Battery::low();
  battery["runtime"] = Battery::runtimeMinutes();

  JsonObject log = doc["log"].to<JsonObject>();
  log["on"] = (bool)Settings::get(S_LOGGING);
  log["interval"] = Settings::value(S_LOG_INTERVAL);
  log["session"] = Logger::currentSession();
  log["records"] = Logger::sessionRecords();
  log["sessions"] = Logger::fileCount();
  log["full"] = Logger::isFull();
  log["auto"] = (bool)Settings::get(S_AUTO_LOG);
  log["armed"] = AutoLog::armed();
  log["stopIn"] = AutoLog::stopCountdownS();
  log["used"] = Logger::usedBytes();
  log["total"] = Logger::totalBytes();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["mode"] = Net::modeText();
  wifi["ap"] = Net::apIp();
  wifi["sta"] = Net::staIp();
  wifi["clients"] = Net::clients();
  wifi["rssi"] = Net::rssi();
  wifi["host"] = Settings::text(S_HOSTNAME);

  JsonObject sys = doc["sys"].to<JsonObject>();
  sys["heap"] = ESP.getFreeHeap();
  sys["minHeap"] = ESP.getMinFreeHeap();
  sys["temp"] = App::chipTemp();
  sys["reset"] = App::resetReason();
  sys["cpu"] = getCpuFrequencyMhz();
  sys["chip"] = String(ESP.getChipModel()) + " rev " + ESP.getChipRevision();
  sys["flash"] = ESP.getFlashChipSize();
  sys["sketch"] = sketchSize;
  sys["sketchFree"] = sketchFree;
  sys["sdk"] = ESP.getSdkVersion();
  sys["build"] = __DATE__ " " __TIME__;

  JsonObject units = doc["units"].to<JsonObject>();
  units["speed"] = Units::speedUnit();
  units["speedF"] = Units::speed(1);
  units["dist"] = Units::distanceUnit();
  units["distF"] = Units::distance(1);
  units["alt"] = Units::altitudeUnit();
  units["altF"] = Units::altitude(1);
  sendJson(doc);
}

void handleHistory() {
  if (!accept()) return;
  JsonDocument doc;
  doc["speedPeriod"] = Settings::value(S_LOG_INTERVAL);
  doc["batteryPeriod"] = kBatteryGraphPeriodS;
  JsonArray speed = doc["speed"].to<JsonArray>();
  for (size_t i = 0; i < histSpeed.size(); i++) speed.add(serialized(String(histSpeed.at(i), 1)));
  JsonArray alt = doc["alt"].to<JsonArray>();
  for (size_t i = 0; i < histAlt.size(); i++) alt.add(serialized(String(histAlt.at(i), 1)));
  JsonArray bat = doc["battery"].to<JsonArray>();
  for (size_t i = 0; i < histBattery.size(); i++) bat.add(serialized(String(histBattery.at(i), 3)));
  sendJson(doc);
}

void handleSettingsGet() {
  if (!accept()) return;
  JsonDocument doc;
  JsonArray groups = doc["groups"].to<JsonArray>();
  for (int g = 0; g < G_COUNT; g++) groups.add(Settings::kGroupNames[g]);

  JsonArray items = doc["items"].to<JsonArray>();
  char label[32];
  for (int i = 0; i < S_COUNT; i++) {
    const SettingDef &d = Settings::def(i);
    JsonObject o = items.add<JsonObject>();
    o["key"] = d.key;
    o["group"] = d.group;
    o["label"] = d.label;
    o["help"] = d.help;
    switch (d.type) {
      case SettingType::Bool:
        o["type"] = "bool";
        o["value"] = (bool)Settings::get(i);
        break;
      case SettingType::Int:
        o["type"] = "int";
        o["min"] = d.min;
        o["max"] = d.max;
        o["step"] = d.step;
        o["dec"] = d.decimals;
        o["unit"] = d.unit;
        o["value"] = Settings::get(i);
        break;
      case SettingType::Choice: {
        o["type"] = "choice";
        JsonArray options = o["options"].to<JsonArray>();
        for (int k = 0; k < Settings::optionCount(i); k++) {
          Settings::optionLabel(i, k, label, sizeof label);
          options.add(String(label));
        }
        o["value"] = Settings::get(i);
        break;
      }
      case SettingType::Text:
        o["type"] = "text";
        o["maxLen"] = d.maxLen;
        o["secret"] = d.secret;
        o["value"] = d.secret ? String() : Settings::text(i);
        if (d.secret) o["isSet"] = !Settings::text(i).isEmpty();
        break;
    }
  }
  sendJson(doc);
}

void handleSettingsPost() {
  if (!accept()) return;
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")) || !body.is<JsonObject>()) {
    sendError(400, "expected a JSON object");
    return;
  }

  JsonDocument result;
  JsonObject errors = result["errors"].to<JsonObject>();
  int changed = 0;
  for (JsonPair kv : body.as<JsonObject>()) {
    const char *key = kv.key().c_str();
    int id = Settings::find(key);
    if (id < 0) {
      errors[key] = "unknown setting";
      continue;
    }
    const SettingDef &d = Settings::def(id);
    JsonVariant v = kv.value();
    bool didChange = false;
    if (d.type == SettingType::Text) {
      if (!v.is<const char *>()) {
        errors[key] = "expected text";
        continue;
      }
      String s = v.as<String>();
      if (id == S_AP_PASS && !s.isEmpty() && s.length() < 8) {
        errors[key] = "at least 8 characters";
        continue;
      }
      if ((id == S_AP_SSID || id == S_HOSTNAME) && s.isEmpty()) {
        errors[key] = "cannot be empty";
        continue;
      }
      didChange = Settings::setText(id, s);
    } else if (v.is<bool>()) {
      didChange = Settings::set(id, v.as<bool>());
    } else if (v.is<float>()) {
      didChange = Settings::set(id, lround(v.as<float>()));
    } else {
      errors[key] = "expected a number";
      continue;
    }
    if (didChange) {
      changed++;
      App::applySetting(id);
    }
  }
  result["ok"] = errors.size() == 0;
  result["changed"] = changed;
  sendJson(result);
}

void handleSessions() {
  if (!accept()) return;
  std::vector<SessionInfo> list = Logger::sessions();
  JsonDocument doc;
  JsonArray sessions = doc["sessions"].to<JsonArray>();
  for (auto it = list.rbegin(); it != list.rend(); ++it) {
    JsonObject o = sessions.add<JsonObject>();
    o["name"] = it->name;
    o["start"] = it->firstEpoch;
    o["end"] = it->lastEpoch;
    o["records"] = it->records;
    o["bytes"] = it->bytes;
    o["recording"] = it->recording;
  }
  doc["used"] = Logger::usedBytes();
  doc["total"] = Logger::totalBytes();
  sendJson(doc);
}

void handleTrack() {
  if (!accept()) return;
  String name = sessionArg();
  File f = Logger::openSession(name);
  if (!f) {
    sendError(404, "session not found");
    return;
  }
  long maxPoints = server.hasArg("max") ? server.arg("max").toInt() : 1500;
  beginStream("application/json", String());
  ChunkedPrint out;
  Export::trackJson(f, name, constrain(maxPoints, 50L, 5000L), out);
  endStream(out);
}

void handleDownload(const String &format) {
  if (!accept()) return;
  String name = sessionArg();
  File f = Logger::openSession(name);
  if (!f) {
    sendError(404, "session not found");
    return;
  }
  String filename = "GPS_" + name + "." + format;
  ChunkedPrint out;
  if (format == "kml") {
    beginStream("application/vnd.google-earth.kml+xml", filename);
    Export::kml(f, name, out);
  } else if (format == "gpx") {
    beginStream("application/gpx+xml", filename);
    Export::gpx(f, name, out);
  } else {
    beginStream("text/csv", filename);
    out.println(Export::kCsvHeader);
    Export::csv(f, out);
  }
  endStream(out);
}

void handleDelete() {
  if (!accept()) return;
  JsonDocument doc;
  doc["ok"] = Logger::removeSession(server.arg("f"));
  sendJson(doc);
}

void handleAction() {
  if (!accept()) return;
  String what = server.arg("do");
  bool ok = true;
  if (what == "logging_on") {
    App::setLogging(true);
  } else if (what == "logging_off") {
    App::setLogging(false);
  } else if (what == "new_session") {
    App::newSession();
  } else if (what == "trip_reset") {
    App::resetTrip();
  } else if (what == "erase_logs") {
    App::eraseLogs();
  } else if (what == "wifi_off") {
    Settings::set(S_WIFI_MODE, WM_OFF);
    App::applySetting(S_WIFI_MODE);
  } else if (what == "restart") {
    App::restart(1500);
  } else if (what == "sleep") {
    App::sleep(1500);
  } else if (what == "factory_reset") {
    App::factoryReset();
  } else {
    ok = false;
  }
  JsonDocument doc;
  doc["ok"] = ok;
  sendJson(doc, ok ? 200 : 400);
}

void handleUpdateDone() {
  if (!accept()) return;
  bool ok = otaAuthorized && otaOk && !Update.hasError();
  JsonDocument doc;
  doc["ok"] = ok;
  if (!ok) doc["error"] = Update.errorString();
  server.sendHeader("Connection", "close");
  sendJson(doc, ok ? 200 : 500);
  if (ok) {
    Ui::showMessage("Updated", "restarting...");
    App::restart(1500);
  }
}

void handleUpdateUpload() {
  HTTPUpload &upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      lastRequest = millis();
      otaAuthorized = passwordOk();
      otaOk = otaAuthorized && Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH);
      if (otaOk) {
        Logger::close();
        Ui::showMessage("Updating", upload.filename.c_str());
      }
      break;
    case UPLOAD_FILE_WRITE:
      if (otaOk && Update.write(upload.buf, upload.currentSize) != upload.currentSize) otaOk = false;
      break;
    case UPLOAD_FILE_END:
      if (otaOk) otaOk = Update.end(true);
      break;
    case UPLOAD_FILE_ABORTED:
      if (otaOk) Update.abort();
      otaOk = false;
      break;
  }
}

void handleLcd() {
  if (!accept()) return;
  const uint8_t *buf = Ui::frameBuffer();
  String hex;
  hex.reserve(128 * 8 * 2 + 64);
  hex += "{\"w\":128,\"h\":64,\"flip\":";
  hex += Ui::frameFlipped() ? "true" : "false";
  hex += ",\"invert\":";
  hex += Settings::get(S_INVERT) ? "true" : "false";
  hex += ",\"hex\":\"";
  char byte[3];
  for (int i = 0; i < 128 * 8; i++) {
    snprintf(byte, sizeof byte, "%02x", buf[i]);
    hex += byte;
  }
  hex += "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", hex);
}

void handleKey() {
  if (!accept()) return;
  String k = server.arg("k");
  Key key = k == "up" ? Key::Up : k == "down" ? Key::Down : k == "push" ? Key::Push : k == "hold" ? Key::LongPush : Key::None;
  if (key != Key::None) Input::inject(key);
  JsonDocument doc;
  doc["ok"] = key != Key::None;
  sendJson(doc, key != Key::None ? 200 : 400);
}

void addRoutes() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/lcd", HTTP_GET, handleLcd);
  server.on("/api/key", HTTP_POST, handleKey);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/api/sessions", HTTP_GET, handleSessions);
  server.on("/api/track", HTTP_GET, handleTrack);
  server.on("/api/delete", HTTP_POST, handleDelete);
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/dl", HTTP_GET, [] { handleDownload(server.arg("fmt")); });
  server.on("/kml", HTTP_GET, [] { handleDownload("kml"); });
  server.on("/csv", HTTP_GET, [] { handleDownload("csv"); });
  server.on("/gpx", HTTP_GET, [] { handleDownload("gpx"); });
  // Captive portal: phones probing for internet on the access point get sent to the app
  server.onNotFound([] {
    lastRequest = millis();
    logRequest(Net::apActive() ? " -> captive redirect" : " -> 404");
    if (Net::apActive()) {
      server.sendHeader("Location", "http://" + Net::apIp() + "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
}

}  // namespace

void Web::start() {
  if (running) return;
  if (!routesAdded) {
    addRoutes();
    routesAdded = true;
  }
  server.begin();
  running = true;
}

void Web::stop() {
  if (!running) return;
  server.stop();
  running = false;
}

void Web::update() {
  if (running) server.handleClient();
}

uint32_t Web::lastRequestMs() { return lastRequest; }

void Web::setDebug(bool on) { debug = on; }

namespace {

// Runs as its own task: the main loop keeps serving while this client reads, so large pages can't deadlock
void selfTestTask(void *param) {
  String *path = static_cast<String *>(param);
  IPAddress ip = Net::apActive() ? WiFi.softAPIP() : WiFi.localIP();
  WiFiClient client;
  uint32_t start = millis();
  if (!client.connect(ip, 80, 3000)) {
    Serial.printf("[webtest] %s: TCP connect failed after %u ms\n", path->c_str(), (unsigned)(millis() - start));
  } else {
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path->c_str(), ip.toString().c_str());
    String head, preview, tail;
    bool inBody = false;
    size_t total = 0;
    uint32_t lastData = millis();
    uint8_t buf[512];
    while (millis() - lastData < 6000) {
      int n = client.read(buf, sizeof buf);
      if (n <= 0) {
        if (!client.connected()) break;
        delay(5);
        continue;
      }
      lastData = millis();
      total += n;
      for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (!inBody) {
          head += c;
          inBody = head.endsWith("\r\n\r\n");
        } else {
          if (preview.length() < 100) preview += c;
          tail += c;
          if (tail.length() > 60) tail.remove(0, tail.length() - 60);
        }
      }
    }
    bool closed = !client.connected();
    client.stop();
    for (String *s : {&preview, &tail}) {
      s->replace("\r", "\\r");
      s->replace("\n", "\\n");
    }
    Serial.printf("[webtest] %s: %u bytes in %u ms, %s\n%s[webtest] body start: %s\n[webtest] body end: %s\n",
                  path->c_str(), (unsigned)total, (unsigned)(millis() - start), closed ? "server closed" : "TIMED OUT",
                  head.c_str(), preview.c_str(), tail.c_str());
  }
  delete path;
  vTaskDelete(nullptr);
}

}  // namespace

void Web::selfTest(const char *path, Print &out) {
  if (!running) {
    out.println("web server is not running (WiFi off?)");
    return;
  }
  out.printf("server listening, AP %s, %d stations; result follows as [webtest]\n", WiFi.softAPIP().toString().c_str(),
             WiFi.softAPgetStationNum());
  xTaskCreate(selfTestTask, "webtest", 8192, new String(path), 1, nullptr);
}
