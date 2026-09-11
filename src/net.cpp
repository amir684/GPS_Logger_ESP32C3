#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "settings.h"
#include "web.h"

namespace {

constexpr uint32_t kHomeFallbackMs = 20000;

DNSServer dns;  // answers every name with the AP address, so phones open the app as a captive portal
bool dnsOn = false;
bool running = false;
bool apOn = false;
bool homeWanted = false;
bool fallbackAp = false;
bool mdnsOn = false;
bool applyPending = false;
uint32_t applyAtMs = 0;
uint32_t startedMs = 0;
uint32_t lastActivityMs = 0;

void startAp() {
  const String &pass = Settings::text(S_AP_PASS);
  // The driver rejects passwords shorter than 8 characters; fall back to an open network
  WiFi.softAP(Settings::text(S_AP_SSID).c_str(), pass.length() >= 8 ? pass.c_str() : nullptr);
  apOn = true;
  if (!dnsOn) dnsOn = dns.start(53, "*", WiFi.softAPIP());
}

void startMdns() {
  if (mdnsOn) MDNS.end();
  mdnsOn = MDNS.begin(Settings::text(S_HOSTNAME).c_str());
  if (mdnsOn) MDNS.addService("http", "tcp", 80);
}

void startNow() {
  int mode = Settings::get(S_WIFI_MODE);
  bool wantAp = mode == WM_AP || mode == WM_BOTH;
  homeWanted = (mode == WM_HOME || mode == WM_BOTH) && !Settings::text(S_STA_SSID).isEmpty();
  if (mode == WM_HOME && !homeWanted) wantAp = true;  // no home network configured yet: stay reachable
  if (!wantAp && !homeWanted) return;

  WiFi.persistent(false);
  WiFi.setHostname(Settings::text(S_HOSTNAME).c_str());
  WiFi.mode(wantAp && homeWanted ? WIFI_AP_STA : wantAp ? WIFI_AP : WIFI_STA);
  apOn = false;
  fallbackAp = false;
  if (wantAp) startAp();
  if (homeWanted) WiFi.begin(Settings::text(S_STA_SSID).c_str(), Settings::text(S_STA_PASS).c_str());
  WiFi.setTxPower((wifi_power_t)Settings::value(S_WIFI_TXPWR));
  startMdns();
  Web::start();
  running = true;
  startedMs = lastActivityMs = millis();
}

}  // namespace

void Net::begin() { startNow(); }

void Net::update() {
  if (applyPending && millis() >= applyAtMs) {
    applyPending = false;
    stop();
    startNow();
  }
  if (!running) return;
  uint32_t now = millis();  // after startNow(), which stamps lastActivityMs
  if (dnsOn) dns.processNextRequest();

  // Home network unreachable: open the access point as well so the device can still be configured
  if (homeWanted && !apOn && WiFi.status() != WL_CONNECTED && now - startedMs > kHomeFallbackMs) {
    WiFi.mode(WIFI_AP_STA);
    startAp();
    fallbackAp = true;
  }

  if (apOn && WiFi.softAPgetStationNum() > 0) lastActivityMs = now;
  uint32_t lastRequest = Web::lastRequestMs();
  if (lastRequest && now - lastRequest < now - lastActivityMs) lastActivityMs = lastRequest;
  uint32_t idleMinutes = Settings::value(S_WIFI_IDLE);
  if (idleMinutes && now - lastActivityMs > idleMinutes * 60000UL) stop();
}

void Net::apply(uint32_t delayMs) {
  applyPending = true;
  applyAtMs = millis() + delayMs;
}

void Net::stop() {
  if (!running) return;
  Web::stop();
  if (dnsOn) {
    dns.stop();
    dnsOn = false;
  }
  if (mdnsOn) {
    MDNS.end();
    mdnsOn = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  running = apOn = homeWanted = fallbackAp = false;
}

bool Net::active() { return running; }

bool Net::apActive() { return running && apOn; }

bool Net::staConnected() { return running && homeWanted && WiFi.status() == WL_CONNECTED; }

String Net::apIp() { return apActive() ? WiFi.softAPIP().toString() : String(); }

String Net::staIp() { return staConnected() ? WiFi.localIP().toString() : String(); }

String Net::primaryIp() { return staConnected() ? staIp() : apIp(); }

int Net::clients() { return apActive() ? WiFi.softAPgetStationNum() : 0; }

int Net::rssi() { return staConnected() ? WiFi.RSSI() : 0; }

const char *Net::modeText() {
  if (!running) return "Off";
  if (fallbackAp) return "Home + fallback AP";
  if (apOn && homeWanted) return "AP + Home";
  return apOn ? "Access point" : "Home WiFi";
}
