#pragma once

#include <Print.h>

#include <cstdint>

// HTTP server: single-page web app plus JSON API. Net starts/stops it with the WiFi interfaces.
namespace Web {
void start();
void stop();
void update();
uint32_t lastRequestMs();

// Diagnostics for the serial console
void setDebug(bool on);                        // log every request
void selfTest(const char *path, Print &out);  // HTTP GET to our own address over the network stack
}  // namespace Web
