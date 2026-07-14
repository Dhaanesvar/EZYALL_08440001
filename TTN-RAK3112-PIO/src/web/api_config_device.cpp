#include "api_config_device.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>

#include "web/portal_page.h"
#include "config/ap_credentials.h"
#include "config/device_config.h"
#include "lora/lora_helpers.h"
#include "network/ap_manager.h"

extern WebServer server;
extern IPAddress g_apIp;
extern IPAddress g_apGw;
extern char g_apSsid[33];
extern bool g_lwActivated;

void syncApIpFromConfig();
void saveConfig();
void addLog(const char* dir, const String& m);
bool doJoin();

static const char* BOARD_IP_DEFAULT_INFO = "13.22.0.213";
static const IPAddress AP_SN_LOCAL(255, 255, 255, 0);

void handleRoot() {
  server.send(200, "text/html", PAGE);
}

void handleGetConfig() {
  JsonDocument doc;
  doc["mode"] = cfg.mode;
  doc["region"] = cfg.region;
  doc["frequencyHz"] = cfg.frequencyHz;
  doc["subBand"] = cfg.subBand;
  doc["adr"] = cfg.adr;
  doc["confirmed"] = cfg.confirmed;
  doc["fPort"] = cfg.fPort;
  doc["txPower"] = cfg.txPower;
  doc["serialBaud"] = cfg.serialBaud;
  doc["joinEUI"] = cfg.joinEUI;
  doc["devEUI"] = cfg.devEUI;
  doc["appKey"] = cfg.appKey;
  doc["nwkKey"] = cfg.nwkKey;
  doc["devAddr"] = cfg.devAddr;
  doc["nwkSKey"] = cfg.nwkSKey;
  doc["appSKey"] = cfg.appSKey;
  doc["lwClass"] = cfg.lwClass;
  doc["customPayload"] = cfg.customPayload;
  doc["intervalSec"] = cfg.intervalSec;
  doc["intervalMin"] = cfg.intervalMin;
  doc["intervalHour"] = cfg.intervalHour;
  doc["intervalDay"] = cfg.intervalDay;
  doc["intervalMonth"] = cfg.intervalMonth;
  doc["uidName"] = cfg.uidName;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleGetDeviceInfo() {
  JsonDocument doc;
  uint64_t efuse = ESP.getEfuseMac();
  char uidHex[17];
  snprintf(uidHex, sizeof(uidHex), "%08lX%08lX", (uint32_t)(efuse >> 32), (uint32_t)efuse);

  doc["uid"] = uidHex;
  doc["uidName"] = cfg.uidName;
  doc["mac"] = WiFi.softAPmacAddress();
  doc["ip"] = g_apIp.toString();
  doc["boardDefaultIp"] = BOARD_IP_DEFAULT_INFO;
  doc["ledUid"] = cfg.ledUid;
  doc["ledMac"] = cfg.ledMac;
  doc["ledIp"] = cfg.ledIp;
  doc["ssid"] = g_apSsid;
  doc["joined"] = g_lwActivated;

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePostDeviceInfo() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }

  String newName = String((const char*)(doc["uidName"] | ""));
  newName.trim();
  if (newName.length() == 0) {
    newName = AP_SSID_DEFAULT;
  }
  if (newName.length() >= (int)sizeof(cfg.uidName)) {
    newName = newName.substring(0, sizeof(cfg.uidName) - 1);
  }

  String newIp = String((const char*)(doc["ip"] | ""));
  newIp.trim();
  if (newIp.length() == 0) {
    newIp = String(cfg.apIp);
  }

  String newLedUid = String((const char*)(doc["ledUid"] | ""));
  newLedUid.trim();
  if (newLedUid.length() == 0) {
    newLedUid = "08-44";
  }
  if (newLedUid.length() >= (int)sizeof(cfg.ledUid)) {
    newLedUid = newLedUid.substring(0, sizeof(cfg.ledUid) - 1);
  }

  String newLedMac = String((const char*)(doc["ledMac"] | ""));
  newLedMac.trim();
  if (newLedMac.length() == 0) {
    newLedMac = "DE:AD:BE:EF:FE:ED";
  }
  if (newLedMac.length() >= (int)sizeof(cfg.ledMac)) {
    newLedMac = newLedMac.substring(0, sizeof(cfg.ledMac) - 1);
  }

  String newLedIp = String((const char*)(doc["ledIp"] | ""));
  newLedIp.trim();
  if (newLedIp.length() == 0) {
    newLedIp = BOARD_IP_DEFAULT_INFO;
  }
  if (newLedIp.length() >= (int)sizeof(cfg.ledIp)) {
    newLedIp = newLedIp.substring(0, sizeof(cfg.ledIp) - 1);
  }
  IPAddress parsedIp;
  if (!parsedIp.fromString(newIp)) {
    server.send(400, "application/json", "{\"error\":\"invalid ip\"}");
    return;
  }

  strlcpy(cfg.uidName, newName.c_str(), sizeof(cfg.uidName));
  strlcpy(cfg.apIp, newIp.c_str(), sizeof(cfg.apIp));
  strlcpy(cfg.ledUid, newLedUid.c_str(), sizeof(cfg.ledUid));
  strlcpy(cfg.ledMac, newLedMac.c_str(), sizeof(cfg.ledMac));
  strlcpy(cfg.ledIp, newLedIp.c_str(), sizeof(cfg.ledIp));
  strlcpy(g_apSsid, cfg.uidName, sizeof(g_apSsid));
  syncApIpFromConfig();
  saveConfig();

  bool apOk = restartAccessPoint(g_apSsid, AP_PASSWORD, g_apIp, g_apGw, AP_SN_LOCAL, 120);
  addLog(apOk ? "INFO" : "ERROR", String("UID/AP updated: ") + g_apSsid + " @ " + g_apIp.toString());

  server.send(200, "application/json", apOk ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  strlcpy(cfg.mode, doc["mode"] | "OTAA", sizeof(cfg.mode));
  strlcpy(cfg.region, doc["region"] | "EU868", sizeof(cfg.region));
  cfg.frequencyHz = doc["frequencyHz"] | defaultHzForRegion(cfg.region);
  cfg.subBand = doc["subBand"] | 0;
  cfg.adr = doc["adr"] | true;
  cfg.confirmed = doc["confirmed"] | false;
  cfg.fPort = doc["fPort"] | 1;
  cfg.txPower = (int8_t)(doc["txPower"] | 14);
  cfg.serialBaud = doc["serialBaud"] | 115200UL;
  strlcpy(cfg.joinEUI, doc["joinEUI"] | "", sizeof(cfg.joinEUI));
  strlcpy(cfg.devEUI, doc["devEUI"] | "", sizeof(cfg.devEUI));
  strlcpy(cfg.appKey, doc["appKey"] | "", sizeof(cfg.appKey));
  strlcpy(cfg.nwkKey, doc["nwkKey"] | "", sizeof(cfg.nwkKey));
  strlcpy(cfg.devAddr, doc["devAddr"] | "", sizeof(cfg.devAddr));
  strlcpy(cfg.nwkSKey, doc["nwkSKey"] | "", sizeof(cfg.nwkSKey));
  strlcpy(cfg.appSKey, doc["appSKey"] | "", sizeof(cfg.appSKey));
  strlcpy(cfg.lwClass, "C", sizeof(cfg.lwClass));
  strlcpy(cfg.customPayload, doc["customPayload"] | "", sizeof(cfg.customPayload));
  cfg.intervalSec = doc["intervalSec"] | 2;
  cfg.intervalMin = doc["intervalMin"] | 0;
  cfg.intervalHour = doc["intervalHour"] | 0;
  cfg.intervalDay = doc["intervalDay"] | 0;
  cfg.intervalMonth = doc["intervalMonth"] | 0;
  saveConfig();
  addLog("INFO", String("Saved: region=") + cfg.region + " Hz=" + cfg.frequencyHz +
                 " subBand=" + cfg.subBand + " mode=" + cfg.mode);
  server.send(200, "application/json", "{\"ok\":true}");
  addLog("INFO", "Auto-join triggered by Save.");
  bool joinSuccess = doJoin();
  server.send(200, "application/json", joinSuccess ? "{\"ok\":true,\"joined\":true}" : "{\"ok\":true,\"joined\":false}");
}
