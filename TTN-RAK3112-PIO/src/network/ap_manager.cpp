#include "ap_manager.h"

bool startAccessPoint(const char* ssid, const char* password,
                      const IPAddress& ip, const IPAddress& gateway,
                      const IPAddress& subnet) {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(ip, gateway, subnet);
  return WiFi.softAP(ssid, password);
}

bool restartAccessPoint(const char* ssid, const char* password,
                        const IPAddress& ip, const IPAddress& gateway,
                        const IPAddress& subnet,
                        uint32_t disconnectDelayMs) {
  WiFi.softAPdisconnect(true);
  delay(disconnectDelayMs);
  WiFi.softAPConfig(ip, gateway, subnet);
  return WiFi.softAP(ssid, password);
}
