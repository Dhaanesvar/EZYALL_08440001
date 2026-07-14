#pragma once

#include <WiFi.h>

bool startAccessPoint(const char* ssid, const char* password,
                      const IPAddress& ip, const IPAddress& gateway,
                      const IPAddress& subnet);

bool restartAccessPoint(const char* ssid, const char* password,
                        const IPAddress& ip, const IPAddress& gateway,
                        const IPAddress& subnet,
                        uint32_t disconnectDelayMs = 120);
