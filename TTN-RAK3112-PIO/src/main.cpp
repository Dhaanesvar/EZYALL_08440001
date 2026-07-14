#include <cstdint>

/**
 * RAK3112 — ESP32-S3 + SX1262 (SPI) — LoRaWAN config portal + live log
 * Modified to forward downlink payloads to Pico over UART1 (safe S3 pins)
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "utils/hex_utils.h"
#include "lora/lora_helpers.h"
#include "network/ap_manager.h"
#include "web/api_config_device.h"
#include "config/ap_credentials.h"
#include "config/pins.h"
#include "config/session_keys.h"
#include "config/device_config.h"

// UART bridge to Pico (user wiring).
static const int PICO_UART_TX_PIN = 32;
static const int PICO_UART_RX_PIN = 33;
HardwareSerial& picoUart = Serial2;
static const bool PICO_UART_BRIDGE_ENABLED = false;

inline void picoUartWriteLine(const char* s) {
  if (!PICO_UART_BRIDGE_ENABLED) return;
  picoUart.println(s);
}

// --- RadioLib ---
#include <RadioLib.h>
Module   sx1262Module(LORA_SX126X_CS, LORA_SX126X_DIO1, LORA_SX126X_RESET, LORA_SX126X_BUSY, SPI);
SX1262   radio(&sx1262Module);
LoRaWANNode* lorawan = nullptr;
static const LoRaWANBand_t* lwBand = &EU868;
static uint8_t lwSubBand = 0;


// ─── WiFi AP ─────────────────────────────────────────────────────────────────
static const IPAddress AP_IP_DEFAULT(192, 168, 4, 1);
static const IPAddress AP_GW_DEFAULT(192, 168, 4, 1);
static const IPAddress AP_SN(255, 255, 255, 0);
static const char* BOARD_IP_DEFAULT_INFO = "13.22.0.213";
char g_apSsid[33] = {0};
IPAddress g_apIp = AP_IP_DEFAULT;
IPAddress g_apGw = AP_GW_DEFAULT;

// ─── Runtime flags ───────────────────────────────────────────────────────────
bool     g_radioPhyReady = false;
bool     g_lwActivated   = false;
uint32_t g_lastUplinkMs  = 0;
unsigned long lastClassCCheck = 0;
unsigned long lastRxProof = 0;

Preferences prefs;
WebServer server(80);

// Parsed binary keys (filled when joining)

// ─── Live log ring buffer ───────────────────────────────────────────────────
#define MAX_LOG 200
struct LogEntry {
  char t[20];
  char dir[10];
  char msg[240];
};
static LogEntry logs[MAX_LOG];
static int logHead = 0;
static int logCount = 0;

// Dedicated downlink buffer for Downlink Monitor
#define MAX_DOWNLINK 50
struct DownlinkEntry {
  char t[20];
  char hex[128];
  char ascii[65];
  int len;
  uint8_t fport;
};
static DownlinkEntry downlinks[MAX_DOWNLINK];
static int downlinkHead = 0;
static int downlinkCount = 0;

// ============ NEW: Downlink State Tracking (TTN Behavior Aware) ============
struct DownlinkStateTracker {
  bool uplinkSent = false;
  uint32_t uplinkTimestamp = 0;
  size_t uplinkPayloadSize = 0;
  uint8_t uplinkFPort = 0;
  bool downlinkReceived = false;
  uint32_t downlinkTimestamp = 0;
  size_t downlinkPayloadSize = 0;
  uint8_t downlinkFPort = 0;
  bool hasAppPayload = false;
  char lastDownlinkHex[256] = "";
  char lastDownlinkAscii[128] = "";
} dlTracker;

// LED status tracking
bool systemJoined = false;       // true after successful join
bool activityActive = false;     // true during downlink activity
unsigned long lastGreenBlink = 0;
bool greenLedOn = false;
bool redBlinkForever = false;    // ADDED: infinite red blink on error

void syncApIpFromConfig() {
  IPAddress parsed;
  if (parsed.fromString(cfg.apIp)) {
    g_apIp = parsed;
    g_apGw = parsed;
  } else {
    g_apIp = AP_IP_DEFAULT;
    g_apGw = AP_GW_DEFAULT;
    strlcpy(cfg.apIp, "192.168.4.1", sizeof(cfg.apIp));
  }
}

// Forward declarations for functions referenced before definition.
int initRadioPhy();
bool createLoRaWAN();
bool doJoin();

void addDownlink(const char* t, const char* hex, const char* ascii, int len, uint8_t fport) {
  DownlinkEntry& e = downlinks[downlinkHead];
  strncpy(e.t, t, sizeof(e.t) - 1);
  e.t[sizeof(e.t) - 1] = 0;
  strncpy(e.hex, hex, sizeof(e.hex) - 1);
  e.hex[sizeof(e.hex) - 1] = 0;
  strncpy(e.ascii, ascii, sizeof(e.ascii) - 1);
  e.ascii[sizeof(e.ascii) - 1] = 0;
  e.len = len;
  e.fport = fport;
  downlinkHead = (downlinkHead + 1) % MAX_DOWNLINK;
  if (downlinkCount < MAX_DOWNLINK) downlinkCount++;
  
  // UART Output
  if (len > 0) {
    Serial.println(ascii);           // existing
    picoUartWriteLine(ascii);
    if (PICO_UART_BRIDGE_ENABLED) {
      Serial.print("[PICO] Sent via UART1: ");
      Serial.println(ascii);
    }
  }
}

void addLog(const char* dir, const String& m) {
  LogEntry& e = logs[logHead];
  unsigned long ms = millis();
  snprintf(e.t, sizeof(e.t), "%lu.%03lu", ms / 1000, ms % 1000);
  strncpy(e.dir, dir, sizeof(e.dir) - 1);
  e.dir[sizeof(e.dir) - 1] = 0;
  strncpy(e.msg, m.c_str(), sizeof(e.msg) - 1);
  e.msg[sizeof(e.msg) - 1] = 0;
  logHead = (logHead + 1) % MAX_LOG;
  if (logCount < MAX_LOG) logCount++;
  // Web live log keeps everything; serial/YAT hides only UP entries.
  if (Serial && strcmp(e.dir, "UP") != 0) {
    Serial.printf("[%s][%s] %s\n", e.t, e.dir, e.msg);
  }
}

void resetRadio() {
  digitalWrite(LORA_SX126X_RESET, LOW);
  delay(50);
  digitalWrite(LORA_SX126X_RESET, HIGH);
  delay(1000);
  // Then re-initialize your LoRaWAN stack:
  if (lorawan) {
    delete lorawan;
    lorawan = nullptr;
  }
  initRadioPhy();
  createLoRaWAN();
  // Re-join or reactivate as needed
  doJoin();  // or re-run join logic
}


// ─── NVS ────────────────────────────────────────────────────────────────────
void loadConfig() {
  prefs.begin(PREFS_NS, true);
  strlcpy(cfg.mode, prefs.getString("mode", "OTAA").c_str(), sizeof(cfg.mode));
  strlcpy(cfg.region, prefs.getString("region", "EU868").c_str(), sizeof(cfg.region));
  cfg.frequencyHz = prefs.getUInt("freqHz", defaultHzForRegion(cfg.region));
  cfg.subBand     = prefs.getUChar("subBand", 0);
  cfg.adr         = prefs.getBool("adr", true);
  cfg.confirmed   = prefs.getBool("confirmed", false);
  cfg.fPort       = prefs.getUChar("fPort", 1);
  cfg.txPower     = (int8_t)prefs.getChar("txPower", 14);
  cfg.serialBaud  = prefs.getUInt("baud", 115200);

  strlcpy(cfg.joinEUI, prefs.getString("joinEUI", "").c_str(), sizeof(cfg.joinEUI));
  strlcpy(cfg.devEUI, prefs.getString("devEUI", "").c_str(), sizeof(cfg.devEUI));
  strlcpy(cfg.appKey, prefs.getString("appKey", "").c_str(), sizeof(cfg.appKey));
  strlcpy(cfg.nwkKey, prefs.getString("nwkKey", "").c_str(), sizeof(cfg.nwkKey));
  strlcpy(cfg.devAddr, prefs.getString("devAddr", "").c_str(), sizeof(cfg.devAddr));
  strlcpy(cfg.nwkSKey, prefs.getString("nwkSKey", "").c_str(), sizeof(cfg.nwkSKey));
  strlcpy(cfg.appSKey, prefs.getString("appSKey", "").c_str(), sizeof(cfg.appSKey));
  // Force Class C
  strlcpy(cfg.lwClass, "C", sizeof(cfg.lwClass));
  cfg.intervalSec = prefs.getUChar("intervalSec", 2);
  cfg.intervalMin = prefs.getUChar("intervalMin", 0);
  cfg.intervalHour = prefs.getUChar("intervalHour", 0);
  cfg.intervalDay = prefs.getUChar("intervalDay", 0);
  cfg.intervalMonth = prefs.getUChar("intervalMonth", 0);
  strlcpy(cfg.uidName, prefs.getString("uidName", AP_SSID_DEFAULT).c_str(), sizeof(cfg.uidName));
  strlcpy(cfg.apIp, prefs.getString("apIp", "192.168.4.1").c_str(), sizeof(cfg.apIp));
  strlcpy(cfg.ledUid, prefs.getString("ledUid", "08-44").c_str(), sizeof(cfg.ledUid));
  strlcpy(cfg.ledMac, prefs.getString("ledMac", "DE:AD:BE:EF:FE:ED").c_str(), sizeof(cfg.ledMac));
  strlcpy(cfg.ledIp, prefs.getString("ledIp", BOARD_IP_DEFAULT_INFO).c_str(), sizeof(cfg.ledIp));
  strlcpy(cfg.customPayload, prefs.getString("customPayload", "").c_str(), sizeof(cfg.customPayload));
  prefs.end();
  strlcpy(cfg.lwClass, "C", sizeof(cfg.lwClass));  // HARD LOCK CLASS C
  syncApIpFromConfig();
}

void saveConfig() {
  prefs.begin(PREFS_NS, false);
  prefs.putString("mode", cfg.mode);
  prefs.putString("region", cfg.region);
  prefs.putUInt("freqHz", cfg.frequencyHz);
  prefs.putUChar("subBand", cfg.subBand);
  prefs.putBool("adr", cfg.adr);
  prefs.putBool("confirmed", cfg.confirmed);
  prefs.putUChar("fPort", cfg.fPort);
  prefs.putChar("txPower", cfg.txPower);
  prefs.putUInt("baud", cfg.serialBaud);
  prefs.putString("joinEUI", cfg.joinEUI);
  prefs.putString("devEUI", cfg.devEUI);
  prefs.putString("appKey", cfg.appKey);
  prefs.putString("nwkKey", cfg.nwkKey);
  prefs.putString("devAddr", cfg.devAddr);
  prefs.putString("nwkSKey", cfg.nwkSKey);
  prefs.putString("appSKey", cfg.appSKey);
  prefs.putString("lwClass", "C"); // Always Class C
  prefs.putString("customPayload", cfg.customPayload);
  prefs.putUChar("intervalSec", cfg.intervalSec);
  prefs.putUChar("intervalMin", cfg.intervalMin);
  prefs.putUChar("intervalHour", cfg.intervalHour);
  prefs.putUChar("intervalDay", cfg.intervalDay);
  prefs.putUChar("intervalMonth", cfg.intervalMonth);
  prefs.putString("uidName", cfg.uidName);
  prefs.putString("apIp", cfg.apIp);
  prefs.putString("ledUid", cfg.ledUid);
  prefs.putString("ledMac", cfg.ledMac);
  prefs.putString("ledIp", cfg.ledIp);
  prefs.end();
  addLog("INFO", "Configuration saved to NVS (Preferences).");
}

// --- ADD THIS FUNCTION HERE ---
bool isConfigValid() {
  if (!strcasecmp(cfg.mode, "OTAA")) {
    return (strlen(cfg.joinEUI) > 0 && strlen(cfg.devEUI) > 0 && strlen(cfg.appKey) > 0);
  } else if (!strcasecmp(cfg.mode, "ABP")) {
    return (strlen(cfg.devAddr) > 0 && strlen(cfg.nwkSKey) > 0 && strlen(cfg.appSKey) > 0);
  }
  return false;
}


void saveLwBuffers() {}
bool restoreLwBuffers() { return false; }
void clearLwBuffers() {}

// --- Prepare RadioLib credentials from config ---
bool prepareCredentials() {
  memset(binJoinEUI, 0, sizeof(binJoinEUI));
  memset(binDevEUI, 0, sizeof(binDevEUI));
  memset(binAppKey, 0, sizeof(binAppKey));
  memset(binNwkKey, 0, sizeof(binNwkKey));
  memset(binNwkSKey, 0, sizeof(binNwkSKey));
  memset(binAppSKey, 0, sizeof(binAppSKey));
  binDevAddr = 0;

  if (!strcasecmp(cfg.mode, "OTAA")) {
    uint64_t joinEUI64 = 0, devEUI64 = 0;
    if (!parseHexU64MsbFirst(cfg.joinEUI, &joinEUI64)) {
      addLog("ERROR", "OTAA: JoinEUI parse failed (expect 16 hex chars, MSB first).");
      redBlinkForever = true;
      return false;
    }
    if (!parseHexU64MsbFirst(cfg.devEUI, &devEUI64)) {
      addLog("ERROR", "OTAA: DevEUI parse failed (expect 16 hex chars, MSB first).");
      redBlinkForever = true;
      return false;
    }
    if (!parseHexString(cfg.appKey, binAppKey, 16)) {
      addLog("ERROR", "OTAA: AppKey parse failed (expect 32 hex chars).");
      redBlinkForever = true;
      return false;
    }
    if (strlen(cfg.nwkKey) >= 32) {
      if (!parseHexString(cfg.nwkKey, binNwkKey, 16)) {
        addLog("ERROR", "OTAA: NwkKey parse failed.");
        redBlinkForever = true;
        return false;
      }
    } else {
      memcpy(binNwkKey, binAppKey, 16); // LoRaWAN 1.0.x TTN: AppKey == NwkKey
    }
    (void)joinEUI64;
    (void)devEUI64;
    return true;
  }

  if (!strcasecmp(cfg.mode, "ABP")) {
    if (!parseHexU32(cfg.devAddr, &binDevAddr)) {
      addLog("ERROR", "ABP: DevAddr parse failed (8 hex chars).");
      redBlinkForever = true;
      return false;
    }
    if (!parseHexString(cfg.nwkSKey, binNwkSKey, 16)) {
      addLog("ERROR", "ABP: NwkSKey parse failed.");
      redBlinkForever = true;
      return false;
    }
    if (!parseHexString(cfg.appSKey, binAppSKey, 16)) {
      addLog("ERROR", "ABP: AppSKey parse failed.");
      redBlinkForever = true;
      return false;
    }
    return true;
  }

  addLog("ERROR", "Unknown mode (use OTAA or ABP).");
  redBlinkForever = true;
  return false;
}

// --- RadioLib radio and node setup/teardown ---
void destroyLoRaWAN() {
  if (lorawan) {
    lorawan->clearSession();
    delete lorawan;
    lorawan = nullptr;
  }
  g_lwActivated = false;
  memset(&dlTracker, 0, sizeof(dlTracker));
}

bool createLoRaWAN() {
  destroyLoRaWAN();
  lwBand = bandPtr(cfg.region);
  lwSubBand = cfg.subBand;
  lorawan = new LoRaWANNode(&radio, lwBand, lwSubBand);
  return lorawan != nullptr;
}

int initRadioPhy() {
  addLog("INIT", "PHY: SPI.begin(SCK,MISO,MOSI,CS) on internal SX1262 bus");
  SPI.begin(LORA_SX126X_SCK, LORA_SX126X_MISO, LORA_SX126X_MOSI, LORA_SX126X_CS);

  float mhz = cfg.frequencyHz / 1e6f;
  addLog("INIT", String("PHY: radio.begin() LoRa ") + mhz + " MHz, 125k, SF7, CR4/5, TCXO 1.8V (RAK3112)");
  int16_t st = radio.begin(
    mhz,
    125.0f,
    7,
    5,
    RADIOLIB_SX126X_SYNC_WORD_PRIVATE,
    cfg.txPower,
    8,
    1.8f,
    false
  );
  if (st != RADIOLIB_ERR_NONE) {
    addLog("ERROR", String("radio.begin failed: ") + lwErrStr(st));
    g_radioPhyReady = false;
    redBlinkForever = true;
    return st;
  }

  g_radioPhyReady = true;
  addLog("INIT", "PHY OK — equivalent to: lora_rak3112_init + Radio.Init + Sleep/SetChannel/SetTx/Rx/Rx()");
  addLog("INIT", "MAC layer will override datarate/channels per band plan during LoRaWAN join/uplink.");
  return RADIOLIB_ERR_NONE;
}

bool doJoin() {
  addLog("INFO", "══ Join / activate started ══");
  if (!prepareCredentials()) {
    redBlinkForever = true;
    return false;
  }

  destroyLoRaWAN();

  if (initRadioPhy() != RADIOLIB_ERR_NONE) {
    redBlinkForever = true;
    return false;
  }

  if (!createLoRaWAN()) {
    addLog("ERROR", "LoRaWANNode allocation failed.");
    redBlinkForever = true;
    return false;
  }

  int16_t st;

  if (!strcasecmp(cfg.mode, "OTAA")) {
    uint64_t joinEUI = 0, devEUI = 0;
    parseHexU64MsbFirst(cfg.joinEUI, &joinEUI);
    parseHexU64MsbFirst(cfg.devEUI, &devEUI);

    st = lorawan->beginOTAA(joinEUI, devEUI, binNwkKey, binAppKey);
    if (st != RADIOLIB_ERR_NONE) {
      addLog("ERROR", String("beginOTAA failed: ") + lwErrStr(st));
      redBlinkForever = true;
      return false;
    }
    addLog("INFO", "OTAA: beginOTAA OK (keys loaded into MAC).");

    if (restoreLwBuffers()) {
      addLog("INFO", "Attempting activateOTAA() with restored NVS session...");
    } else {
      addLog("INFO", "No valid NVS session — full OTAA join will run.");
    }

    st = lorawan->activateOTAA();

    if (st != RADIOLIB_LORAWAN_NEW_SESSION && st != RADIOLIB_LORAWAN_SESSION_RESTORED) {
      addLog("ERROR", String("activateOTAA failed: ") + lwErrStr(st));
      addLog("ERROR", "Check TTN keys, region, sub-band, gateway coverage, and 'Resets join nonces' if testing.");
      redBlinkForever = true;
      return false;
    }

    addLog("UP", st == RADIOLIB_LORAWAN_NEW_SESSION
                ? "OTAA NEW SESSION — Join-Accept received (device ↔ gateway ↔ TTN join path OK)."
                : "OTAA SESSION RESTORED from NVS (no new Join-Accept this boot).");
  } else {
    st = lorawan->beginABP(binDevAddr, nullptr, nullptr, binNwkSKey, binAppSKey);
    if (st != RADIOLIB_ERR_NONE) {
      addLog("ERROR", String("beginABP failed: ") + lwErrStr(st));
      redBlinkForever = true;
      return false;
    }
    st = lorawan->activateABP();
    if (st != RADIOLIB_ERR_NONE) {
      addLog("ERROR", String("activateABP failed: ") + lwErrStr(st));
      redBlinkForever = true;
      return false;
    }
    addLog("INFO", "ABP session activated (no Join-Accept; DevAddr + session keys programmed).");
  }

  lorawan->setADR(cfg.adr);
  lorawan->setTxPower(cfg.txPower);
  int16_t classResult = lorawan->setClass(RADIOLIB_LORAWAN_CLASS_C);
  addLog("INFO", "CLASS C FORCED (hard lock active)");
  addLog("DEBUG", String("setClass(Class C) result: ") + classResult);
  addLog("DEBUG", String("Region: ") + cfg.region + ", subBand: " + cfg.subBand);

  saveLwBuffers();
  g_lwActivated = lorawan->isActivated();
  g_lastUplinkMs = millis();
  addLog("INFO", String("LoRaWAN activated. ADR=") + (cfg.adr ? "on" : "off") +
                 " confirmedUL=" + (cfg.confirmed ? "on" : "off") +
                 " FPort=" + cfg.fPort + " txPow=" + cfg.txPower + " dBm");

  // Send a confirmed empty uplink to resync TTN session (fixes DevAddr stale mappings)
  uint8_t dummy[1] = {0};
  st = lorawan->sendReceive(dummy, 1, cfg.fPort, nullptr, nullptr, true);
  if (st >= 0) {
    addLog("INFO", "Confirmed uplink sent to resync TTN session (DevAddr sync)");
  } else {
    addLog("WARN", "Confirmed uplink resync failed: " + lwErrStr(st));
  }
  // ===================================

  saveLwBuffers();
  
  systemJoined = true;  // LED: Green ON when joined
  redBlinkForever = false;  // Clear any previous error flag
  
  return true;
}

void doLeave() {
  destroyLoRaWAN();
  clearLwBuffers();
  g_radioPhyReady = false;
  memset(&dlTracker, 0, sizeof(dlTracker));
  addLog("INFO", "Left LoRaWAN — session cleared. Re-Join required.");
  systemJoined = false;
}

// ─── Uplink interval ──────────────────────────────────────────────────────────
uint32_t getUplinkIntervalMs() {
  uint32_t ms = 0;
  ms += (uint32_t)cfg.intervalSec * 1000UL;
  ms += (uint32_t)cfg.intervalMin * 60UL * 1000UL;
  ms += (uint32_t)cfg.intervalHour * 60UL * 60UL * 1000UL;
  ms += (uint32_t)cfg.intervalDay * 24UL * 60UL * 60UL * 1000UL;
  ms += (uint32_t)cfg.intervalMonth * 30UL * 24UL * 60UL * 60UL * 1000UL;
  if (ms == 0) ms = 2000UL;
  return ms;
}

// ─── HTTP handlers ───────────────────────────────────────────────────────────

void handleGetLogs() {
  const int kMaxLines = 120;
  JsonDocument doc;
  JsonArray arr = doc["logs"].to<JsonArray>();
  int n = logCount < kMaxLines ? logCount : kMaxLines;
  int start = logCount >= MAX_LOG ? (logHead - n + MAX_LOG) % MAX_LOG : 0;
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % MAX_LOG;
    JsonObject o = arr.add<JsonObject>();
    o["t"] = logs[idx].t;
    o["d"] = logs[idx].dir;
    String m = logs[idx].msg;
    m.replace("\\", "\\\\");
    m.replace("\"", "'");
    o["m"] = m;
  }
  doc["joined"] = g_lwActivated;
  doc["phy"] = g_radioPhyReady;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleClearLogs() {
  logHead = 0;
  logCount = 0;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleGetDownlink() {
  JsonDocument doc;
  JsonArray arr = doc["downlink"].to<JsonArray>();
  int n = downlinkCount < MAX_DOWNLINK ? downlinkCount : MAX_DOWNLINK;
  int start = downlinkCount >= MAX_DOWNLINK ? (downlinkHead - n + MAX_DOWNLINK) % MAX_DOWNLINK : 0;
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % MAX_DOWNLINK;
    JsonObject o = arr.add<JsonObject>();
    o["t"] = downlinks[idx].t;
    o["hex"] = downlinks[idx].hex;
    o["ascii"] = downlinks[idx].ascii;
    o["len"] = downlinks[idx].len;
    o["fport"] = downlinks[idx].fport;
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleGetDlState() {
  JsonDocument doc;
  doc["uplinkSent"] = dlTracker.uplinkSent;
  doc["uplinkTimestamp"] = dlTracker.uplinkTimestamp;
  doc["uplinkPayloadSize"] = dlTracker.uplinkPayloadSize;
  doc["uplinkFPort"] = dlTracker.uplinkFPort;
  doc["downlinkReceived"] = dlTracker.downlinkReceived;
  doc["downlinkPayloadSize"] = dlTracker.downlinkPayloadSize;
  doc["downlinkFPort"] = dlTracker.downlinkFPort;
  doc["hasAppPayload"] = dlTracker.hasAppPayload;
  
  uint32_t interval = getUplinkIntervalMs();
  uint32_t timeSince = (dlTracker.uplinkTimestamp > 0) ? (millis() - dlTracker.uplinkTimestamp) : 0;
  doc["nextUplinkSec"] = (timeSince >= interval) ? 0 : ((interval - timeSince) / 1000);
  doc["timeSinceUplinkSec"] = timeSince / 1000;
  doc["intervalSec"] = interval / 1000;
  
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleJoinReq() {
  bool ok = doJoin();
  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void handleLeaveReq() {
  doLeave();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleForceUplink() {
  if (lorawan && lorawan->isActivated()) {
    addLog("INFO", "Force uplink triggered manually");
    
    const char* payload = cfg.customPayload[0] ? cfg.customPayload : "hi and im Dhaanes";
    size_t payloadLen = strlen(payload);
    
    uint8_t down[256];
    size_t downLen = 0;
    
    addLog("UP", String("Force uplink → FPort=") + cfg.fPort + " \"" + payload + "\" (" + payloadLen + " B)");
    
    int16_t st = lorawan->sendReceive(
      (const uint8_t*)payload,
      payloadLen,
      cfg.fPort,
      down,
      &downLen,
      cfg.confirmed
    );
    
    dlTracker.uplinkSent = true;
    dlTracker.uplinkTimestamp = millis();
    dlTracker.uplinkPayloadSize = payloadLen;
    dlTracker.uplinkFPort = cfg.fPort;
    
    if (st == RADIOLIB_ERR_NONE) {
      addLog("INFO", "Force uplink OK - no downlink queued");
      dlTracker.downlinkReceived = false;
    } else if (st > 0 && downLen > 0) {
      activityActive = true;
      Serial.println(">>> DOWNLINK DETECTED - LED SHOULD BLINK <<<");
      
      String hx, asc;
      for (size_t i = 0; i < downLen; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02X", down[i]);
        hx += b;
        asc += (down[i] >= 32 && down[i] <= 126) ? (char)down[i] : '.';
      }
      unsigned long ms = millis();
      char t[20];
      snprintf(t, sizeof(t), "%lu.%03lu", ms / 1000, ms % 1000);
      uint8_t rxFPort = (uint8_t)st;
      addDownlink(t, hx.c_str(), asc.c_str(), (int)downLen, rxFPort);
      
      Serial.println(asc);
      picoUartWriteLine(asc.c_str());
      if (PICO_UART_BRIDGE_ENABLED) {
        picoUart.print("[PICO] Sent via UART1 (force): ");
        picoUart.println(asc);
      }
      
      dlTracker.downlinkReceived = true;
      dlTracker.downlinkTimestamp = ms;
      dlTracker.downlinkPayloadSize = downLen;
      dlTracker.downlinkFPort = (uint8_t)st;
      dlTracker.hasAppPayload = (downLen > 0);
      
      addLog("DOWN", String("Force uplink downlink: ") + downLen + " B port=" + st);
      digitalWrite(PIN_LED2, HIGH);
      delay(80);
      digitalWrite(PIN_LED2, LOW);
    } else if (st > 0) {
      addLog("DEBUG", "MAC command received (no app payload)");
    } else {
      addLog("ERROR", String("Force uplink failed: ") + lwErrStr(st));
      redBlinkForever = true;
    }
    
    g_lastUplinkMs = millis();
    saveLwBuffers();
  } else {
    addLog("ERROR", "Cannot force uplink - device not activated");
    redBlinkForever = true;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSerialCommands() {
  static String line;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      line.trim();
      if (line.length() > 0 && line.equalsIgnoreCase("getuid")) {
        Serial.println("[LED-BOARD] UID=" + String(cfg.ledUid));
        Serial.println("[LED-BOARD] MAC=" + String(cfg.ledMac));
        Serial.println("[LED-BOARD] IP=" + String(cfg.ledIp));
        addLog("INFO", "YAT getuid requested LED board identity.");
      }
      line = "";
    } else if (c >= 32 && c <= 126) {
      if (line.length() < 96) {
        line += c;
      }
    }
  }
}

// ─── setup / loop ────────────────────────────────────────────────────────────
void setup() {
  loadConfig();
  strlcpy(g_apSsid, cfg.uidName[0] ? cfg.uidName : AP_SSID_DEFAULT, sizeof(g_apSsid));
  Serial.begin(cfg.serialBaud);
  // Initialize UART1 bridge to Pico.
  if (PICO_UART_BRIDGE_ENABLED) {
    picoUart.begin(115200, SERIAL_8N1, PICO_UART_RX_PIN, PICO_UART_TX_PIN);
    addLog("INFO", String("Pico UART bridge enabled TX=") + PICO_UART_TX_PIN + " RX=" + PICO_UART_RX_PIN);
  } else {
    addLog("WARN", "Pico UART bridge disabled (safe boot mode)");
  }
  delay(300);

  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  digitalWrite(PIN_LED1, LOW);
  digitalWrite(PIN_LED2, LOW);

  // Initialize status LEDs
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, HIGH);    // RED OFF (active LOW)
  digitalWrite(LED_GREEN, HIGH);  // GREEN OFF

  addLog("INIT", "RAK3112 LoRaWAN portal boot");
  addLog("INIT", String("Serial baud=") + cfg.serialBaud);

  if (startAccessPoint(g_apSsid, AP_PASSWORD, g_apIp, g_apGw, AP_SN)) {
    addLog("INIT", String("AP ") + g_apSsid + " → http://" + g_apIp.toString());
  } else {
    addLog("ERROR", "AP start failed");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/deviceinfo", HTTP_GET, handleGetDeviceInfo);
  server.on("/api/deviceinfo", HTTP_POST, handlePostDeviceInfo);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/logs/clear", HTTP_POST, handleClearLogs);
  server.on("/api/downlink", HTTP_GET, handleGetDownlink);
  server.on("/api/dlstate", HTTP_GET, handleGetDlState);
  server.on("/api/join", HTTP_POST, handleJoinReq);
  server.on("/api/leave", HTTP_POST, handleLeaveReq);
  server.on("/api/forceuplink", HTTP_POST, handleForceUplink);
  server.begin();

  addLog("INFO", "Load TTN keys, Save, then Join.");
    // Auto-join on boot if configuration is valid
  if (isConfigValid()) {
    addLog("INFO", "Valid configuration found, auto-joining...");
    doJoin();
  } else {
    addLog("WARN", "Incomplete configuration. Please enter keys and save, then join manually.");
  }
}

// ─── Periodic uplink + FIXED downlink reception ───────────────────────────────
void doPeriodicUplink() {
  if (!lorawan || !lorawan->isActivated()) return;
  lorawan->setClass(RADIOLIB_LORAWAN_CLASS_C);
  uint32_t interval = getUplinkIntervalMs();
  if (millis() - g_lastUplinkMs < interval) return;
  g_lastUplinkMs = millis();

  const char* payload = cfg.customPayload[0] ? cfg.customPayload : "hi and im Dhaanes";
  size_t payloadLen = strlen(payload);

  uint8_t down[256];
  size_t downLen = 0;

  addLog("UP", String("Uplink → TTN FPort=") + cfg.fPort +
               " \"" + payload + "\" (" + payloadLen + " B)");

  int16_t st = lorawan->sendReceive(
    (const uint8_t*)payload,
    payloadLen,
    cfg.fPort,
    down,
    &downLen,
    cfg.confirmed
  );

  dlTracker.uplinkSent = true;
  dlTracker.uplinkTimestamp = millis();
  dlTracker.uplinkPayloadSize = payloadLen;
  dlTracker.uplinkFPort = cfg.fPort;

  if (st == RADIOLIB_ERR_NONE) {
    addLog("INFO", "Uplink OK — no downlink payload queued at TTN.");
    dlTracker.downlinkReceived = false;
  } else if (st > 0 && downLen > 0) {
    // ========== DOWNLINK RECEIVED - BLINK GREEN LED ==========
    picoUart.println(">>> DOWNLINK PAYLOAD RECEIVED - BLINKING GREEN LED <<<");
    
    // BLINK GREEN LED
    for (int blink = 0; blink < 7; blink++) {
      digitalWrite(LED_GREEN, LOW);   // ON
      delay(100);
      digitalWrite(LED_GREEN, HIGH);  // OFF
      delay(100);
    }
    
    String hx, asc;
    for (size_t i = 0; i < downLen; i++) {
      char b[4];
      snprintf(b, sizeof(b), "%02X", down[i]);
      hx += b;
      char ac = (down[i] >= 32 && down[i] <= 126) ? (char)down[i] : '.';
      asc += ac;
    }
    unsigned long ms = millis();
    char t[20];
    snprintf(t, sizeof(t), "%lu.%03lu", ms / 1000, ms % 1000);
    uint8_t rxFPort = (uint8_t)st;
    
    addDownlink(t, hx.c_str(), asc.c_str(), (int)downLen, rxFPort);
    
    if (downLen > 0) {
      picoUartWriteLine(asc.c_str());
      if (PICO_UART_BRIDGE_ENABLED) {
        picoUart.print("[PICO] Sent via UART1 (downlink): ");
        picoUart.println(asc);
      }
    }
    
    dlTracker.downlinkReceived = true;
    dlTracker.downlinkTimestamp = ms;
    dlTracker.downlinkPayloadSize = downLen;
    dlTracker.downlinkFPort = rxFPort;
    dlTracker.hasAppPayload = (downLen > 0);
    strncpy(dlTracker.lastDownlinkHex, hx.c_str(), sizeof(dlTracker.lastDownlinkHex)-1);
    strncpy(dlTracker.lastDownlinkAscii, asc.c_str(), sizeof(dlTracker.lastDownlinkAscii)-1);
    
    addLog("DOWN", String("Downlink from TTN: ") + downLen +
                   " B port=" + rxFPort + " hex=" + hx + " ascii='" + asc + "'");
    digitalWrite(PIN_LED2, HIGH);
    delay(80);
    digitalWrite(PIN_LED2, LOW);
    
    // Return to steady green
    digitalWrite(LED_GREEN, LOW);
    // ===================================================
    
  } else if (st > 0) {
    addLog("DEBUG", "MAC command only (no LED blink)");
  } else {
    addLog("ERROR", String("sendReceive failed: ") + lwErrStr(st));
    redBlinkForever = true;
  }

  saveLwBuffers();
}

void verifyClassC() {
  if (!lorawan) return;
  if (millis() - lastClassCCheck < 2000) return;
  lastClassCCheck = millis();
  bool active = lorawan->isActivated();
  if (active) {
    digitalWrite(PIN_LED1, HIGH);
    delay(20);
    digitalWrite(PIN_LED1, LOW);
  }
}

void checkClassCDownlink() {
  if (!lorawan || !lorawan->isActivated()) return;
  uint8_t downlinkPayload[255];
  size_t downlinkLen = 0;
  LoRaWANEvent_t downlinkEvent;
  int16_t state = lorawan->getDownlinkClassC(downlinkPayload, &downlinkLen, &downlinkEvent);
  if (state > 0 && downlinkLen > 0) {
    activityActive = true;
    
    String hx, asc;
    for (size_t i = 0; i < downlinkLen; i++) {
      char b[4];
      snprintf(b, sizeof(b), "%02X", downlinkPayload[i]);
      hx += b;
      char ac = (downlinkPayload[i] >= 32 && downlinkPayload[i] <= 126) ? (char)downlinkPayload[i] : '.';
      asc += ac;
    }
    unsigned long ms = millis();
    char t[20];
    snprintf(t, sizeof(t), "%lu.%03lu", ms / 1000, ms % 1000);
    uint8_t rxFPort = downlinkEvent.fPort;
    addDownlink(t, hx.c_str(), asc.c_str(), (int)downlinkLen, rxFPort);
    addLog("DOWN", String("Class C downlink: ") + downlinkLen +
                   " B port=" + rxFPort + " hex=" + hx + " ascii='" + asc + "'");
    
    // UART Output - Class C Downlink
    Serial.println(asc);
    picoUartWriteLine(asc.c_str());
    if (PICO_UART_BRIDGE_ENABLED) {
      picoUart.print("[PICO] Sent via UART1 (Class C): ");
      picoUart.println(asc);
    }
    
    dlTracker.downlinkReceived = true;
    dlTracker.downlinkTimestamp = ms;
    dlTracker.downlinkPayloadSize = downlinkLen;
    dlTracker.downlinkFPort = rxFPort;
    dlTracker.hasAppPayload = true;
    
    digitalWrite(PIN_LED2, HIGH);
    delay(80);
    digitalWrite(PIN_LED2, LOW);
  } else if (state < 0) {
    redBlinkForever = true;
  }
}

void loop() {
  server.handleClient();
  handleSerialCommands();
  doPeriodicUplink();
  checkClassCDownlink();
  verifyClassC();

  static unsigned long lastReboot = 0;
  if (millis() - lastReboot > 86400000UL) { // 24 hours
    lastReboot = millis();
    ESP.restart();
  }

  // LED status update
  if (redBlinkForever) {
    // INFINITE RED BLINK ON ERROR
    if (millis() - lastGreenBlink >= 500) {
      lastGreenBlink = millis();
      digitalWrite(LED_RED, !digitalRead(LED_RED));
    }
    digitalWrite(LED_GREEN, HIGH);  // GREEN OFF
  } else if (systemJoined) {
    digitalWrite(LED_RED, HIGH);    // RED OFF
    digitalWrite(LED_GREEN, LOW);   // GREEN STEADY ON
  } else {
    digitalWrite(LED_RED, HIGH);    // RED OFF
    digitalWrite(LED_GREEN, HIGH);  // GREEN OFF
  }
}
