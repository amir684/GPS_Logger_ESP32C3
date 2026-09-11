#pragma once

#include <Arduino.h>
#include <FS.h>

#include <vector>

constexpr uint8_t kLogFlagFix = 0x01;
constexpr size_t kLogHeaderSize = 8;

// One sample on flash. Keep the size fixed; new sensors go into aux[].
struct __attribute__((packed)) LogRecord {
  uint32_t epoch;  // UTC seconds
  int32_t latE7;
  int32_t lonE7;
  int32_t altCm;
  uint16_t speedCms;
  uint16_t courseCdeg;
  uint16_t hdopC;  // HDOP x100, 0xFFFF = unknown
  uint16_t batteryMv;
  uint8_t sats;
  uint8_t flags;
  int16_t aux[3];
};
static_assert(sizeof(LogRecord) == 32, "LogRecord layout changed");

struct SessionInfo {
  String name;  // UTC start time, "YYYYMMDD_HHMMSS"
  size_t bytes;
  uint32_t records;
  uint32_t firstEpoch;
  uint32_t lastEpoch;
  bool recording;
};

// One binary file per logging session: /log/YYYYMMDD_HHMMSS.bin (UTC start time).
// The oldest sessions are deleted when flash fills up.
namespace Logger {
bool begin();
bool append(const LogRecord &record);  // the first record after boot or close() starts a new session
void flush();
void close();
uint32_t sessionRecords();
size_t totalBytes();
size_t usedBytes();
int fileCount();
std::vector<SessionInfo> sessions();  // oldest first
String latestSession();
bool isValidName(const String &name);
File openSession(const String &name);  // positioned at the first record; invalid File if missing or unknown format
bool removeSession(const String &name);
void removeAll();
void setStopWhenFull(bool stop);  // false = delete the oldest sessions instead
bool isFull();                    // logging stopped because flash is full
String currentSession();          // empty when no session is open
}  // namespace Logger
