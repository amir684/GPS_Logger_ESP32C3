#include "menu.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include <algorithm>

#include "app.h"
#include "battery.h"
#include "gps.h"
#include "logger.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include "version.h"

namespace {

constexpr int kVisibleRows = 6;
constexpr int kRowHeight = 9;
constexpr int kListTop = 10;
constexpr int kMaxRows = 32;
constexpr int kMaxDepth = 4;
constexpr int kMaxValueWidth = 84;

enum PageKind : uint8_t { P_ROOT, P_GROUP, P_INFO };
enum RowKind : uint8_t { R_BACK, R_PAGE, R_SETTING, R_ACTION, R_INFO };
enum ActionId : uint8_t {
  A_LOGGING,
  A_WIFI,
  A_NEW_SESSION,
  A_RESET_TRIP,
  A_ERASE_LOGS,
  A_FACTORY,
  A_RESTART,
  A_SLEEP,
  A_EXIT,
};
enum InfoId : uint8_t {
  I_VERSION,
  I_UPTIME,
  I_HEAP,
  I_TEMP,
  I_RESET,
  I_MAC,
  I_STORAGE,
  I_SESSIONS,
  I_WIFI,
  I_IP,
  I_CLIENTS,
  I_RSSI,
  I_BATTERY,
  I_RUNTIME,
  I_NMEA,
  I_COUNT,
};
enum class Mode : uint8_t { List, Edit, Confirm };

struct Page {
  PageKind kind;
  uint8_t arg;  // group index for P_GROUP
};

struct Row {
  RowKind kind;
  uint8_t ref;  // page: group index (G_COUNT = info), setting id, action id or info id
};

const char *const kActionLabels[] = {"Logging", "WiFi",          "New session", "Reset trip", "Erase all logs",
                                     "Factory reset", "Restart", "Sleep",       "Exit menu"};
const char *const kInfoLabels[I_COUNT] = {"Firmware", "Uptime",   "Free heap", "Chip temp", "Last reset",
                                          "MAC",      "Storage",  "Sessions",  "WiFi",      "IP",
                                          "Clients",  "Signal",   "Battery",   "Runtime",   "NMEA chars"};

bool menuOpen = false;
Mode mode = Mode::List;
Page stack[kMaxDepth];
int cursor[kMaxDepth];
int top[kMaxDepth];
int depth = 0;
Row rows[kMaxRows];
int rowCount = 0;
int editId = -1;
int32_t editOriginal = 0;
int confirmAction = -1;
bool confirmYes = false;

void addRow(RowKind kind, uint8_t ref) {
  if (rowCount < kMaxRows) rows[rowCount++] = Row{kind, ref};
}

void buildRows() {
  rowCount = 0;
  const Page &page = stack[depth];
  if (page.kind == P_ROOT) {
    addRow(R_ACTION, A_LOGGING);
    addRow(R_ACTION, A_WIFI);
    addRow(R_ACTION, A_SLEEP);
    addRow(R_ACTION, A_NEW_SESSION);
    addRow(R_ACTION, A_RESET_TRIP);
    for (int g = 0; g < G_COUNT; g++) addRow(R_PAGE, g);
    addRow(R_PAGE, G_COUNT);
    addRow(R_ACTION, A_EXIT);
    return;
  }

  addRow(R_BACK, 0);
  if (page.kind == P_INFO) {
    for (int i = 0; i < I_COUNT; i++) addRow(R_INFO, i);
    return;
  }
  for (int i = 0; i < S_COUNT; i++) {
    if (Settings::def(i).group == page.arg) addRow(R_SETTING, i);
  }
  switch (page.arg) {
    case G_LOGGING:
      addRow(R_ACTION, A_NEW_SESSION);
      addRow(R_INFO, I_STORAGE);
      addRow(R_INFO, I_SESSIONS);
      break;
    case G_GPS:
      addRow(R_INFO, I_NMEA);
      break;
    case G_BATTERY:
      addRow(R_INFO, I_BATTERY);
      addRow(R_INFO, I_RUNTIME);
      break;
    case G_WIFI:
      addRow(R_INFO, I_WIFI);
      addRow(R_INFO, I_IP);
      addRow(R_INFO, I_CLIENTS);
      addRow(R_INFO, I_RSSI);
      break;
    case G_SYSTEM:
      addRow(R_ACTION, A_ERASE_LOGS);
      addRow(R_ACTION, A_FACTORY);
      addRow(R_ACTION, A_RESTART);
      addRow(R_ACTION, A_SLEEP);
      break;
  }
}

void fixScroll() {
  int &c = cursor[depth];
  int &t = top[depth];
  if (rowCount == 0) return;
  c = constrain(c, 0, rowCount - 1);
  if (c < t) t = c;
  if (c >= t + kVisibleRows) t = c - kVisibleRows + 1;
}

const char *pageTitle() {
  const Page &p = stack[depth];
  if (p.kind == P_ROOT) return "MENU";
  if (p.kind == P_INFO) return "INFO";
  return Settings::kGroupNames[p.arg];
}

void infoValue(int id, char *out, size_t n) {
  switch (id) {
    case I_VERSION:
      snprintf(out, n, "%s", FW_VERSION);
      break;
    case I_UPTIME: {
      unsigned long s = millis() / 1000;
      if (s >= 86400) {
        snprintf(out, n, "%lud %02lu:%02lu", s / 86400, s / 3600 % 24, s / 60 % 60);
      } else {
        snprintf(out, n, "%02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
      }
      break;
    }
    case I_HEAP:
      snprintf(out, n, "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
      break;
    case I_TEMP:
      snprintf(out, n, "%.1f C", App::chipTemp());
      break;
    case I_RESET:
      snprintf(out, n, "%s", App::resetReason());
      break;
    case I_MAC: {
      uint64_t mac = ESP.getEfuseMac();
      snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)(mac & 0xFF), (unsigned)((mac >> 8) & 0xFF),
               (unsigned)((mac >> 16) & 0xFF), (unsigned)((mac >> 24) & 0xFF), (unsigned)((mac >> 32) & 0xFF),
               (unsigned)((mac >> 40) & 0xFF));
      break;
    }
    case I_STORAGE:
      snprintf(out, n, "%.1f/%.1f MB", Logger::usedBytes() / 1048576.0, Logger::totalBytes() / 1048576.0);
      break;
    case I_SESSIONS:
      snprintf(out, n, "%d", Logger::fileCount());
      break;
    case I_WIFI:
      snprintf(out, n, "%s", Net::modeText());
      break;
    case I_IP: {
      String ip = Net::primaryIp();
      snprintf(out, n, "%s", ip.isEmpty() ? "-" : ip.c_str());
      break;
    }
    case I_CLIENTS:
      snprintf(out, n, "%d", Net::clients());
      break;
    case I_RSSI:
      if (Net::staConnected()) {
        snprintf(out, n, "%d dBm", Net::rssi());
      } else {
        snprintf(out, n, "-");
      }
      break;
    case I_BATTERY:
      if (Battery::present()) {
        snprintf(out, n, "%.2f V %d%%", Battery::volts(), Battery::percent());
      } else {
        snprintf(out, n, "USB power");
      }
      break;
    case I_RUNTIME: {
      int minutes = Battery::runtimeMinutes();
      if (minutes < 0) {
        snprintf(out, n, "-");
      } else {
        snprintf(out, n, "~%dh %02dm", minutes / 60, minutes % 60);
      }
      break;
    }
    case I_NMEA:
      snprintf(out, n, "%lu", (unsigned long)Gps::raw().charsProcessed());
      break;
  }
}

const char *rowLabel(const Row &r) {
  switch (r.kind) {
    case R_BACK: return "< Back";
    case R_PAGE: return r.ref < G_COUNT ? Settings::kGroupNames[r.ref] : "Info";
    case R_SETTING: return Settings::def(r.ref).label;
    case R_ACTION: return kActionLabels[r.ref];
    case R_INFO: return kInfoLabels[r.ref];
  }
  return "";
}

void rowValue(const Row &r, char *out, size_t n) {
  out[0] = '\0';
  switch (r.kind) {
    case R_PAGE:
      snprintf(out, n, ">");
      break;
    case R_SETTING:
      Settings::format(r.ref, out, n);
      break;
    case R_ACTION:
      if (r.ref == A_LOGGING) {
        snprintf(out, n, "%s", !Settings::get(S_LOGGING) ? "OFF" : Logger::isFull() ? "FULL" : "ON");
      } else if (r.ref == A_WIFI) {
        snprintf(out, n, "%s", Net::active() ? "ON" : "OFF");
      }
      break;
    case R_INFO:
      infoValue(r.ref, out, n);
      break;
    default:
      break;
  }
}

bool needsConfirm(int action) {
  return action == A_ERASE_LOGS || action == A_FACTORY || action == A_RESTART || action == A_SLEEP;
}

void runAction(int action) {
  switch (action) {
    case A_LOGGING:
      App::setLogging(!Settings::get(S_LOGGING));
      break;
    case A_WIFI:
      App::setWifi(!Net::active());
      break;
    case A_NEW_SESSION:
      App::newSession();
      break;
    case A_RESET_TRIP:
      App::resetTrip();
      break;
    case A_ERASE_LOGS:
      App::eraseLogs();
      break;
    case A_FACTORY:
      App::factoryReset();
      break;
    case A_RESTART:
      App::restart();
      break;
    case A_SLEEP:
      App::sleep();
      break;
    case A_EXIT:
      Menu::close();
      break;
  }
}

void goBack() {
  if (depth == 0) {
    Menu::close();
    return;
  }
  depth--;
  buildRows();
}

void activate() {
  const Row r = rows[cursor[depth]];
  switch (r.kind) {
    case R_BACK:
      goBack();
      break;
    case R_PAGE:
      if (depth + 1 < kMaxDepth) {
        depth++;
        stack[depth] = Page{r.ref < G_COUNT ? P_GROUP : P_INFO, r.ref};
        cursor[depth] = 0;
        top[depth] = 0;
        buildRows();
      }
      break;
    case R_SETTING: {
      const SettingDef &d = Settings::def(r.ref);
      if (d.type == SettingType::Bool) {
        if (Settings::step(r.ref, 1)) App::applySetting(r.ref);
      } else if (d.type == SettingType::Text) {
        Ui::toast("Edit on web page");
      } else {
        editId = r.ref;
        editOriginal = Settings::get(r.ref);
        mode = Mode::Edit;
      }
      break;
    }
    case R_ACTION:
      if (needsConfirm(r.ref)) {
        confirmAction = r.ref;
        confirmYes = false;
        mode = Mode::Confirm;
      } else {
        runAction(r.ref);
      }
      break;
    default:
      break;
  }
}

void drawCentered(U8G2 &lcd, const char *text, int y) { lcd.drawStr((128 - lcd.getStrWidth(text)) / 2, y, text); }

void fitText(U8G2 &lcd, char *text, int maxWidth) {
  size_t len = strlen(text);
  while (len > 0 && lcd.getStrWidth(text) > maxWidth) text[--len] = '\0';
}

void drawPopupFrame(U8G2 &lcd) {
  lcd.setDrawColor(0);
  lcd.drawBox(4, 13, 120, 50);
  lcd.setDrawColor(1);
  lcd.drawRFrame(4, 13, 120, 50, 3);
}

void drawButton(U8G2 &lcd, int x, int y, int w, int h, const char *text, bool selected) {
  if (selected) {
    lcd.drawRBox(x, y, w, h, 2);
  } else {
    lcd.drawRFrame(x, y, w, h, 2);
  }
  lcd.setDrawColor(selected ? 0 : 1);
  lcd.drawStr(x + (w - lcd.getStrWidth(text)) / 2, y + h - 3, text);
  lcd.setDrawColor(1);
}

void drawList(U8G2 &lcd) {
  lcd.setFont(u8g2_font_5x8_tf);
  lcd.drawStr(0, 7, pageTitle());
  char pos[12];
  snprintf(pos, sizeof pos, "%d/%d", cursor[depth] + 1, rowCount);
  lcd.drawStr(128 - lcd.getStrWidth(pos), 7, pos);
  lcd.drawHLine(0, 8, 128);

  bool scroll = rowCount > kVisibleRows;
  int right = scroll ? 123 : 127;
  char value[48];
  char label[32];
  for (int i = 0; i < kVisibleRows && top[depth] + i < rowCount; i++) {
    int index = top[depth] + i;
    const Row &r = rows[index];
    int y = kListTop + i * kRowHeight;
    bool selected = index == cursor[depth];

    rowValue(r, value, sizeof value);
    fitText(lcd, value, kMaxValueWidth);
    int valueWidth = lcd.getStrWidth(value);
    strlcpy(label, rowLabel(r), sizeof label);
    fitText(lcd, label, right - valueWidth - 8);

    if (selected) {
      lcd.drawBox(0, y, right + 1, kRowHeight);
      lcd.setDrawColor(0);
    }
    lcd.drawStr(2, y + 7, label);
    lcd.drawStr(right - 1 - valueWidth, y + 7, value);
    lcd.setDrawColor(1);
  }

  if (scroll) {
    int trackHeight = 64 - kListTop;
    int thumbHeight = std::max(4, trackHeight * kVisibleRows / rowCount);
    int thumbY = kListTop + (trackHeight - thumbHeight) * top[depth] / (rowCount - kVisibleRows);
    for (int y = kListTop; y < 64; y += 2) lcd.drawPixel(126, y);
    lcd.drawBox(125, thumbY, 3, thumbHeight);
  }
}

void drawEdit(U8G2 &lcd) {
  const SettingDef &d = Settings::def(editId);
  char value[40];
  char shown[48];
  Settings::format(editId, value, sizeof value);
  drawPopupFrame(lcd);

  lcd.setFont(u8g2_font_5x8_tf);
  drawCentered(lcd, d.label, 23);
  lcd.setFont(u8g2_font_6x10_tf);
  snprintf(shown, sizeof shown, "< %s >", value);
  fitText(lcd, shown, 114);
  drawCentered(lcd, shown, 37);

  if (d.type == SettingType::Int && d.max > d.min) {
    lcd.drawFrame(12, 42, 104, 6);
    lcd.drawBox(14, 44, 100 * (Settings::get(editId) - d.min) / (d.max - d.min), 2);
  } else if (d.type == SettingType::Choice) {
    char pos[12];
    snprintf(pos, sizeof pos, "%d / %d", Settings::get(editId) + 1, Settings::optionCount(editId));
    lcd.setFont(u8g2_font_4x6_tf);
    drawCentered(lcd, pos, 48);
  }
  lcd.setFont(u8g2_font_4x6_tf);
  drawCentered(lcd, "push = OK    hold = cancel", 59);
}

void drawConfirm(U8G2 &lcd) {
  drawPopupFrame(lcd);
  char question[32];
  snprintf(question, sizeof question, "%s?", kActionLabels[confirmAction]);
  lcd.setFont(u8g2_font_6x10_tf);
  drawCentered(lcd, question, 27);
  lcd.setFont(u8g2_font_4x6_tf);
  if (confirmAction == A_ERASE_LOGS || confirmAction == A_FACTORY) drawCentered(lcd, "This cannot be undone", 36);
  if (confirmAction == A_SLEEP) drawCentered(lcd, "Push the wheel to wake up", 36);
  lcd.setFont(u8g2_font_6x10_tf);
  drawButton(lcd, 18, 43, 38, 14, "No", !confirmYes);
  drawButton(lcd, 72, 43, 38, 14, "Yes", confirmYes);
}

}  // namespace

void Menu::open() {
  depth = 0;
  stack[0] = Page{P_ROOT, 0};
  cursor[0] = 0;
  top[0] = 0;
  mode = Mode::List;
  buildRows();
  menuOpen = true;
}

void Menu::close() {
  mode = Mode::List;
  menuOpen = false;
}

bool Menu::isOpen() { return menuOpen; }

void Menu::handleKey(Key key) {
  switch (mode) {
    case Mode::Edit:
      if (key == Key::Up || key == Key::Down) {
        if (Settings::step(editId, key == Key::Up ? 1 : -1)) App::applySetting(editId);
      } else if (key == Key::Push) {
        mode = Mode::List;
      } else if (key == Key::LongPush) {
        if (Settings::set(editId, editOriginal)) App::applySetting(editId);
        mode = Mode::List;
      }
      return;

    case Mode::Confirm:
      if (key == Key::Up || key == Key::Down) {
        confirmYes = !confirmYes;
      } else if (key == Key::Push) {
        mode = Mode::List;
        if (confirmYes) runAction(confirmAction);
      } else if (key == Key::LongPush) {
        mode = Mode::List;
      }
      return;

    case Mode::List:
      if (rowCount == 0) return;
      if (key == Key::Up) {
        cursor[depth] = (cursor[depth] + rowCount - 1) % rowCount;
      } else if (key == Key::Down) {
        cursor[depth] = (cursor[depth] + 1) % rowCount;
      } else if (key == Key::Push) {
        activate();
      } else if (key == Key::LongPush) {
        goBack();
      }
      fixScroll();
      return;
  }
}

void Menu::draw(U8G2 &lcd) {
  drawList(lcd);
  if (mode == Mode::Edit) drawEdit(lcd);
  if (mode == Mode::Confirm) drawConfirm(lcd);
}
