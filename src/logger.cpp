#include "logger.h"

#include <LittleFS.h>
#include <time.h>

#include <algorithm>

namespace {

constexpr const char *kDir = "/log";
constexpr uint32_t kFlushPeriodMs = 15000;
constexpr uint32_t kUsagePeriodMs = 10000;
constexpr double kMaxFill = 0.90;
constexpr uint16_t kFormatVersion = 1;

struct __attribute__((packed)) FileHeader {
  char magic[4];
  uint16_t version;
  uint16_t recordSize;
};
static_assert(sizeof(FileHeader) == kLogHeaderSize, "FileHeader size");

bool mounted = false;
bool stopWhenFull = false;
bool full = false;
File current;
String currentName;
uint32_t lastFlushMs = 0;
uint32_t recordCount = 0;
size_t cachedTotal = 0;
size_t cachedUsed = 0;
int cachedFiles = 0;
uint32_t usageCheckedMs = 0;

String pathFor(const String &name) { return String(kDir) + "/" + name + ".bin"; }

std::vector<String> sessionNames() {
  std::vector<String> names;
  File dir = LittleFS.open(kDir);
  if (!dir || !dir.isDirectory()) return names;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    if (f.isDirectory() || !name.endsWith(".bin")) continue;
    name.remove(name.length() - 4);
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

// usedBytes() walks the whole filesystem, so results are cached
void refreshUsage() {
  cachedTotal = LittleFS.totalBytes();
  cachedUsed = LittleFS.usedBytes();
  cachedFiles = sessionNames().size();
  usageCheckedMs = millis();
}

void refreshIfStale() {
  if (mounted && millis() - usageCheckedMs >= kUsagePeriodMs) refreshUsage();
}

void rotate() {
  refreshUsage();
  while (cachedUsed > cachedTotal * kMaxFill) {
    std::vector<String> names = sessionNames();
    if (names.empty() || names.front() == currentName) break;
    LittleFS.remove(pathFor(names.front()));
    refreshUsage();
  }
}

bool startSession(uint32_t epoch) {
  if (stopWhenFull) {
    refreshUsage();
    full = cachedUsed > cachedTotal * kMaxFill;
    if (full) return false;
  } else {
    rotate();
  }
  char name[20];
  time_t t = epoch;
  tm utc;
  gmtime_r(&t, &utc);
  strftime(name, sizeof name, "%Y%m%d_%H%M%S", &utc);

  current = LittleFS.open(pathFor(name), FILE_APPEND);
  if (!current) return false;
  if (current.size() == 0) {
    FileHeader header;
    memcpy(header.magic, "GLOG", 4);
    header.version = kFormatVersion;
    header.recordSize = sizeof(LogRecord);
    current.write((const uint8_t *)&header, sizeof header);
  }
  currentName = name;
  recordCount = 0;
  lastFlushMs = millis();
  refreshUsage();
  return true;
}

bool readHeader(File &f) {
  FileHeader header;
  return f.read((uint8_t *)&header, sizeof header) == sizeof header && memcmp(header.magic, "GLOG", 4) == 0 &&
         header.recordSize == sizeof(LogRecord);
}

uint32_t readEpochAt(File &f, size_t offset) {
  LogRecord r;
  if (!f.seek(offset) || f.read((uint8_t *)&r, sizeof r) != sizeof r) return 0;
  return r.epoch;
}

}  // namespace

bool Logger::begin() {
  mounted = LittleFS.begin(true);
  if (!mounted) return false;
  if (!LittleFS.exists(kDir)) LittleFS.mkdir(kDir);
  refreshUsage();
  return true;
}

bool Logger::append(const LogRecord &record) {
  if (!mounted) return false;
  if (!current && !startSession(record.epoch)) return false;
  if (current.write((const uint8_t *)&record, sizeof record) != sizeof record) return false;
  recordCount++;

  if (millis() - lastFlushMs >= kFlushPeriodMs) {
    flush();
    refreshIfStale();
    if (cachedUsed > cachedTotal * kMaxFill) {
      if (stopWhenFull) {
        full = true;
        close();
      } else {
        rotate();
      }
    }
  }
  return true;
}

void Logger::setStopWhenFull(bool stop) { stopWhenFull = stop; }

bool Logger::isFull() { return full; }

String Logger::currentSession() { return currentName; }

void Logger::flush() {
  if (current) current.flush();
  lastFlushMs = millis();
}

void Logger::close() {
  if (current) current.close();
  currentName = "";
}

uint32_t Logger::sessionRecords() { return recordCount; }

size_t Logger::totalBytes() {
  refreshIfStale();
  return cachedTotal;
}

size_t Logger::usedBytes() {
  refreshIfStale();
  return cachedUsed;
}

int Logger::fileCount() {
  refreshIfStale();
  return cachedFiles;
}

std::vector<SessionInfo> Logger::sessions() {
  std::vector<SessionInfo> list;
  if (!mounted) return list;
  flush();
  for (const String &name : sessionNames()) {
    File f = LittleFS.open(pathFor(name), FILE_READ);
    if (!f) continue;
    SessionInfo s;
    s.name = name;
    s.bytes = f.size();
    s.records = s.bytes > kLogHeaderSize ? (s.bytes - kLogHeaderSize) / sizeof(LogRecord) : 0;
    s.firstEpoch = s.records ? readEpochAt(f, kLogHeaderSize) : 0;
    s.lastEpoch = s.records ? readEpochAt(f, kLogHeaderSize + (s.records - 1) * sizeof(LogRecord)) : 0;
    s.recording = name == currentName;
    list.push_back(s);
  }
  return list;
}

String Logger::latestSession() {
  std::vector<String> names = sessionNames();
  return names.empty() ? String() : names.back();
}

bool Logger::isValidName(const String &name) {
  if (name.length() != 15) return false;
  for (unsigned i = 0; i < 15; i++) {
    char c = name[i];
    if (i == 8 ? c != '_' : !isdigit((unsigned char)c)) return false;
  }
  return true;
}

File Logger::openSession(const String &name) {
  if (!mounted || !isValidName(name)) return File();
  if (name == currentName) flush();
  String path = pathFor(name);
  if (!LittleFS.exists(path)) return File();
  File f = LittleFS.open(path, FILE_READ);
  if (!f || !readHeader(f)) return File();
  return f;
}

bool Logger::removeSession(const String &name) {
  if (!mounted || !isValidName(name)) return false;
  if (name == currentName) close();
  bool ok = LittleFS.remove(pathFor(name));
  refreshUsage();
  full = false;
  return ok;
}

void Logger::removeAll() {
  if (!mounted) return;
  close();
  for (const String &name : sessionNames()) LittleFS.remove(pathFor(name));
  refreshUsage();
  full = false;
}
