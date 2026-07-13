#include <cstdint>

/**
 * RAK3112 — ESP32-S3 + SX1262 (SPI) — LoRaWAN config portal + live log
 * Modified to forward downlink payloads to Pico over Serial0 (UART0, pins J6-8)
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ─── Pins (RAK3112 internal SX1262 SPI) ───────────────────────────────────────
#ifndef LORA_SX126X_SCK
#define LORA_SX126X_SCK     5
#define LORA_SX126X_MISO    3
#define LORA_SX126X_MOSI    6
#define LORA_SX126X_CS      7
#define LORA_SX126X_RESET   8
#define LORA_SX126X_DIO1    47
#define LORA_SX126X_BUSY    48
#endif
#ifndef PIN_LED1
#define PIN_LED1  46
#define PIN_LED2  45
#endif

// LED status indicators
#define LED_RED     35
#define LED_GREEN   34

// --- RadioLib ---
#include <RadioLib.h>
Module   sx1262Module(LORA_SX126X_CS, LORA_SX126X_DIO1, LORA_SX126X_RESET, LORA_SX126X_BUSY, SPI);
SX1262   radio(&sx1262Module);
LoRaWANNode* lorawan = nullptr;
static const LoRaWANBand_t* lwBand = &EU868;
static uint8_t lwSubBand = 0;

// Dedicated UART link to Pico (TX=GPIO32, RX=GPIO33).
HardwareSerial picoUart(1);
static const int PICO_UART_TX_PIN = 32;
static const int PICO_UART_RX_PIN = 33;
static const bool ENABLE_PICO_UART = false;

// --- Session keys (filled from config) ---
uint8_t binJoinEUI[8];
uint8_t binDevEUI[8];
uint8_t binAppKey[16];
uint8_t binNwkKey[16];
uint8_t binNwkSKey[16];
uint8_t binAppSKey[16];
uint32_t binDevAddr = 0;

// ─── WiFi AP ─────────────────────────────────────────────────────────────────
static const char* AP_SSID     = "CRE8IOT_08440001";
static const char* AP_PASSWORD = "";   // min 8 chars
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_SN(255, 255, 255, 0);

// ─── Runtime flags ───────────────────────────────────────────────────────────
bool     g_radioPhyReady = false;
bool     g_lwActivated   = false;
uint32_t g_lastUplinkMs  = 0;
unsigned long lastClassCCheck = 0;
unsigned long lastRxProof = 0;

// ─── Config (persisted) ──────────────────────────────────────────────────────
struct DeviceConfig {
  char mode[8];           // "OTAA" / "ABP"
  char region[16];       // EU868, US915, ...
  uint32_t frequencyHz;   // informational default for region; MAC uses band plan
  uint8_t subBand;        // US915/AU915/CN470 — see RadioLib notes (often 2 for US915)
  bool adr;
  bool confirmed;
  uint8_t fPort;
  int8_t txPower;
  uint32_t serialBaud;  // applied after reboot
  // OTAA
  char joinEUI[32];       // hex string, 16 chars typical
  char devEUI[32];
  char appKey[64];        // 32 hex chars
  char nwkKey[64];        // LoRaWAN 1.1 / optional; if empty, appKey used for OTAA 1.0
  // ABP
  char devAddr[16];       // 8 hex chars
  char nwkSKey[64];
  char appSKey[64];
  // --- Class C and interval scheduler additions ---
  char lwClass[4];         // Always "C" (Class C only)
  uint8_t intervalSec;     // Health ping interval (seconds)
  uint8_t intervalMin;     // Health ping interval (minutes)
  uint8_t intervalHour;    // Health ping interval (hours)
  uint8_t intervalDay;     // Health ping interval (days)
  uint8_t intervalMonth;   // Health ping interval (months)
  char customPayload[128]; // Custom uplink payload (ASCII)
} cfg;

Preferences prefs;
WebServer server(80);

// Parsed binary keys (filled when joining)

#define PREFS_NS "lwcfg"

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

// Forward declarations for functions used before their definitions.
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
    if (ENABLE_PICO_UART) picoUart.println(ascii);  // send to Pico
    // --- ADD THIS LINE ---
    Serial.print("[PICO] Sent via UART1: ");
    Serial.println(ascii);
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
  if (Serial) Serial.printf("[%s][%s] %s\n", e.t, e.dir, e.msg);
}

// ─── Helpers: hex ───────────────────────────────────────────────────────────
static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

bool parseHexString(const char* s, uint8_t* out, size_t nBytes) {
  if (!s) return false;
  size_t len = strlen(s);
  char tmp[128];
  size_t j = 0;
  for (size_t i = 0; i < len && j < sizeof(tmp) - 1; i++) {
    char c = s[i];
    if (c == ' ' || c == ':' || c == '-') continue;
    tmp[j++] = c;
  }
  tmp[j] = 0;
  if (strlen(tmp) != nBytes * 2) return false;
  for (size_t i = 0; i < nBytes; i++) {
    int hi = hexVal(tmp[i * 2]);
    int lo = hexVal(tmp[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

bool parseHexU64MsbFirst(const char* s, uint64_t* out) {
  uint8_t b[8];
  if (!parseHexString(s, b, 8)) return false;
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
  *out = v;
  return true;
}

bool parseHexU32(const char* s, uint32_t* out) {
  uint8_t b[4];
  if (!parseHexString(s, b, 4)) return false;
  *out = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
  return true;
}

// ─── Region → band pointer + default Hz ──────────────────────
uint32_t defaultHzForRegion(const char* reg) {
  if (!strcmp(reg, "EU868")) return 868100000UL;
  if (!strcmp(reg, "EU433")) return 433175000UL;
  if (!strcmp(reg, "US915")) return 902300000UL;
  if (!strcmp(reg, "AU915")) return 915200000UL;
  if (!strcmp(reg, "AS923")) return 923200000UL;
  if (!strcmp(reg, "AS923_2")) return 923200000UL;
  if (!strcmp(reg, "AS923_3")) return 923200000UL;
  if (!strcmp(reg, "AS923_4")) return 923200000UL;
  if (!strcmp(reg, "KR920"))  return 922100000UL;
  if (!strcmp(reg, "IN865"))  return 865062500UL;
  if (!strcmp(reg, "CN470"))  return 470300000UL;
  return 868100000UL;
}

const LoRaWANBand_t* bandPtr(const char* reg) {
  if (!strcmp(reg, "EU868")) return &EU868;
  if (!strcmp(reg, "EU433")) return &EU433;
  if (!strcmp(reg, "US915")) return &US915;
  if (!strcmp(reg, "AU915")) return &AU915;
  if (!strcmp(reg, "AS923")) return &AS923;
  if (!strcmp(reg, "AS923_2")) return &AS923_2;
  if (!strcmp(reg, "AS923_3")) return &AS923_3;
  if (!strcmp(reg, "AS923_4")) return &AS923_4;
  if (!strcmp(reg, "KR920"))  return &KR920;
  if (!strcmp(reg, "IN865"))  return &IN865;
  if (!strcmp(reg, "CN470"))  return &CN470;
  return &EU868;
}

String lwErrStr(int16_t e) {
  return String(e) + " (" + String((int)e) + ")";
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
void loadConfigDefaults() {
  strlcpy(cfg.mode, "OTAA", sizeof(cfg.mode));
  strlcpy(cfg.region, "EU868", sizeof(cfg.region));
  cfg.frequencyHz = defaultHzForRegion(cfg.region);
  cfg.subBand = 0;
  cfg.adr = true;
  cfg.confirmed = false;
  cfg.fPort = 1;
  cfg.txPower = 14;
  cfg.serialBaud = 115200;
  cfg.joinEUI[0] = 0;
  cfg.devEUI[0] = 0;
  cfg.appKey[0] = 0;
  cfg.nwkKey[0] = 0;
  cfg.devAddr[0] = 0;
  cfg.nwkSKey[0] = 0;
  cfg.appSKey[0] = 0;
  strlcpy(cfg.lwClass, "C", sizeof(cfg.lwClass));
  cfg.intervalSec = 0;
  cfg.intervalMin = 0;
  cfg.intervalHour = 0;
  cfg.intervalDay = 0;
  cfg.intervalMonth = 0;
  cfg.customPayload[0] = 0;
}

void loadConfig() {
  loadConfigDefaults();

  if (!prefs.begin(PREFS_NS, false)) {
    return;
  }

  if (prefs.isKey("mode")) {
    strlcpy(cfg.mode, prefs.getString("mode", cfg.mode).c_str(), sizeof(cfg.mode));
  }
  if (prefs.isKey("region")) {
    strlcpy(cfg.region, prefs.getString("region", cfg.region).c_str(), sizeof(cfg.region));
  }
  if (prefs.isKey("freqHz")) cfg.frequencyHz = prefs.getUInt("freqHz", cfg.frequencyHz);
  if (prefs.isKey("subBand")) cfg.subBand = prefs.getUChar("subBand", cfg.subBand);
  if (prefs.isKey("adr")) cfg.adr = prefs.getBool("adr", cfg.adr);
  if (prefs.isKey("confirmed")) cfg.confirmed = prefs.getBool("confirmed", cfg.confirmed);
  if (prefs.isKey("fPort")) cfg.fPort = prefs.getUChar("fPort", cfg.fPort);
  if (prefs.isKey("txPower")) cfg.txPower = (int8_t)prefs.getChar("txPower", cfg.txPower);
  if (prefs.isKey("baud")) cfg.serialBaud = prefs.getUInt("baud", cfg.serialBaud);

  if (prefs.isKey("joinEUI")) strlcpy(cfg.joinEUI, prefs.getString("joinEUI", cfg.joinEUI).c_str(), sizeof(cfg.joinEUI));
  if (prefs.isKey("devEUI")) strlcpy(cfg.devEUI, prefs.getString("devEUI", cfg.devEUI).c_str(), sizeof(cfg.devEUI));
  if (prefs.isKey("appKey")) strlcpy(cfg.appKey, prefs.getString("appKey", cfg.appKey).c_str(), sizeof(cfg.appKey));
  if (prefs.isKey("nwkKey")) strlcpy(cfg.nwkKey, prefs.getString("nwkKey", cfg.nwkKey).c_str(), sizeof(cfg.nwkKey));
  if (prefs.isKey("devAddr")) strlcpy(cfg.devAddr, prefs.getString("devAddr", cfg.devAddr).c_str(), sizeof(cfg.devAddr));
  if (prefs.isKey("nwkSKey")) strlcpy(cfg.nwkSKey, prefs.getString("nwkSKey", cfg.nwkSKey).c_str(), sizeof(cfg.nwkSKey));
  if (prefs.isKey("appSKey")) strlcpy(cfg.appSKey, prefs.getString("appSKey", cfg.appSKey).c_str(), sizeof(cfg.appSKey));
  // Force Class C
  strlcpy(cfg.lwClass, "C", sizeof(cfg.lwClass));
  if (prefs.isKey("intervalSec")) cfg.intervalSec = prefs.getUChar("intervalSec", cfg.intervalSec);
  if (prefs.isKey("intervalMin")) cfg.intervalMin = prefs.getUChar("intervalMin", cfg.intervalMin);
  if (prefs.isKey("intervalHour")) cfg.intervalHour = prefs.getUChar("intervalHour", cfg.intervalHour);
  if (prefs.isKey("intervalDay")) cfg.intervalDay = prefs.getUChar("intervalDay", cfg.intervalDay);
  if (prefs.isKey("intervalMonth")) cfg.intervalMonth = prefs.getUChar("intervalMonth", cfg.intervalMonth);
  if (prefs.isKey("customPayload")) strlcpy(cfg.customPayload, prefs.getString("customPayload", cfg.customPayload).c_str(), sizeof(cfg.customPayload));
  prefs.end();
  strlcpy(cfg.lwClass, "C", sizeof(cfg.lwClass));  // HARD LOCK CLASS C
}

void saveConfig() {
  if (!prefs.begin(PREFS_NS, false)) {
    addLog("ERROR", "Cannot open NVS for save");
    return;
  }
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
  if (ms == 0) ms = 60000UL;
  return ms;
}

// ─── HTTP: embedded UI (unchanged, same as original) ─────────────────────────
static const char PAGE[] = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>RAK3112 LoRaWAN</title>
<style>
*{box-sizing:border-box;margin:0;padding:0;}
body{font-family:system-ui,-apple-system,sans-serif;background:#f5f7fa;color:#1a1d23;font-size:14px;padding-bottom:20px;}

/* ── Login ── */
#loginPage{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px;}
.login-card{background:#fff;border:1px solid #e0e4ec;border-radius:14px;padding:30px 20px;width:100%;max-width:340px;box-shadow:0 2px 16px rgba(0,0,0,.07);}
.login-card h2{font-size:20px;font-weight:600;color:#1a1d23;margin-bottom:6px;}
.login-card p{font-size:13px;color:#6b7280;margin-bottom:24px;}
.login-card input{width:100%;padding:12px;border:1px solid #d1d5db;border-radius:8px;font-size:16px;color:#1a1d23;background:#fff;outline:none;}
.login-card input:focus{border-color:#4f6ef7;}
.login-card .err{color:#dc2626;font-size:12px;margin-top:6px;min-height:16px;}
.login-card button{width:100%;margin-top:18px;padding:12px;background:#4f6ef7;color:#fff;border:none;border-radius:8px;font-size:16px;font-weight:600;cursor:pointer;}

/* ── Layout ── */
header{background:#fff;border-bottom:1px solid #e0e4ec;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px;}
header h1{font-size:14px;font-weight:600;color:#1a1d23;}
.status-pill{display:inline-flex;align-items:center;gap:6px;padding:4px 12px;border-radius:20px;font-size:12px;font-weight:500;}
.pill-ok{background:#dcfce7;color:#166534;}
.pill-err{background:#fee2e2;color:#991b1b;}
.pill-idle{background:#f1f5f9;color:#475569;}
.dot{width:7px;height:7px;border-radius:50%;background:currentColor;opacity:.75;}

.page{padding:16px;}
.card{background:#fff;border:1px solid #e0e4ec;border-radius:12px;padding:16px;margin-bottom:16px;}
.card h2{font-size:13px;font-weight:600;color:#6b7280;text-transform:uppercase;letter-spacing:.06em;margin-bottom:16px;}

/* ── Form ── */
.formgrid{display:flex;flex-direction:column;gap:12px;}
label{display:block;font-size:12px;font-weight:500;color:#374151;margin-bottom:4px;}
input,select{width:100%;padding:11px;border:1px solid #d1d5db;border-radius:7px;font-size:16px;color:#1a1d23;background:#fff;outline:none;}
.check-row{display:flex;align-items:center;gap:8px;padding:9px 0;}
.check-row input[type=checkbox]{width:20px;height:20px;accent-color:#4f6ef7;cursor:pointer;}

.interval-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:8px;}
.interval-grid .lbl{font-size:11px;color:#6b7280;margin-bottom:3px;font-weight:500;}

/* ── Buttons ── */
.btn-row{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px;}
.btn{padding:11px 16px;border-radius:7px;border:none;cursor:pointer;font-size:14px;font-weight:600;flex:1;min-width:100px;}
.btn-save{background:#4f6ef7;color:#fff;}
.btn-join{background:#059669;color:#fff;}
.btn-leave{background:#f1f5f9;color:#475569;border:1px solid #e0e4ec;}

/* ── Log ── */
#log{font-family:'SF Mono',monospace;font-size:10px;background:#f8fafc;border:1px solid #e0e4ec;border-radius:8px;padding:10px;height:200px;overflow-y:auto;line-height:1.4;color:#1e293b;word-break:break-all;}
.log-up{color:#0369a1;}
.log-down{color:#7c3aed;}
.log-err{color:#dc2626;}

/* ── NEW: Downlink State Monitor Box ── */
#dlStateBox{background:#f8fafc;border:1px solid #e0e4ec;border-radius:8px;padding:12px;margin-top:12px;}
.dl-row{padding:6px 0;border-bottom:1px solid #e0e4ec;display:flex;flex-wrap:wrap;justify-content:space-between;}
.dl-row:last-child{border-bottom:none;}
.dl-label{color:#6b7280;font-size:12px;font-weight:500;}
.dl-value{color:#1a1d23;font-size:13px;font-weight:500;text-align:right;}
.dl-highlight{color:#059669;font-weight:600;}
.dl-warning{color:#d97706;}

/* ─── Downlink monitor ── */
#downlinkBox{background:#f8fafc;border:1px solid #e0e4ec;border-radius:8px;padding:10px;min-height:70px;font-family:monospace;font-size:10px;line-height:1.4;color:#1e293b;overflow-y:auto;max-height:150px;}
.dl-port{background:#ede9fe;color:#5b21b6;border-radius:4px;padding:2px 6px;font-size:10px;display:inline-block;margin-right:4px;}
</style>
</head><body>

<!-- ═══ LOGIN ═══ -->
<div id="loginPage">
  <div class="login-card">
    <h2>RAK3112 Portal</h2>
    <p>Enter portal password</p>
    <input id="pass" type="password" placeholder="Password" onkeydown="if(event.key==='Enter')doLogin()">
    <div class="err" id="err"></div>
    <button onclick="doLogin()">Sign in</button>
  </div>
</div>

<!-- ═══ MAIN ═══ -->
<div id="mainPage" style="display:none">

<header>
  <h1>RAK3112 LoRaWAN</h1>
  <span class="status-pill pill-idle" id="statusPill"><span class="dot"></span><span id="statusTxt">Idle</span></span>
</header>

<div class="page">

  <!-- Config -->
  <div class="card">
    <h2>Configuration</h2>
    <div class="formgrid">
      <div>
        <label>Mode</label>
        <select id="mode">
          <option value="OTAA">OTAA</option>
          <option value="ABP">ABP</option>
        </select>
      </div>

      <div id="f_joinEUI"><label>JoinEUI</label><input id="joinEUI" placeholder="0000000000000000"></div>
      <div id="f_devEUI"><label>DevEUI</label><input id="devEUI" placeholder="0000000000000000"></div>
      <div id="f_appKey"><label>AppKey</label><input id="appKey" placeholder="32 hex chars"></div>
      <div id="f_nwkKey"><label>NwkKey (optional)</label><input id="nwkKey" placeholder="Leave blank for 1.0.x"></div>

      <div id="f_devAddr" style="display:none"><label>DevAddr</label><input id="devAddr" placeholder="00000000"></div>
      <div id="f_nwkSKey" style="display:none"><label>NwkSKey</label><input id="nwkSKey"></div>
      <div id="f_appSKey" style="display:none"><label>AppSKey</label><input id="appSKey"></div>

      <div>
        <label>Region</label>
        <select id="region">
          <option>EU868</option><option>US915</option><option>AU915</option><option>AS923</option>
          <option>KR920</option><option>IN865</option><option>CN470</option>
        </select>
      </div>
      <div>
        <label>FPort</label>
        <input id="fPort" type="number" value="1">
      </div>
      <div>
        <label>TX Power (dBm)</label>
        <input id="txPower" type="number" value="14">
      </div>

      <div class="check-row"><input type="checkbox" id="adr"><span>ADR</span></div>
      <div class="check-row"><input type="checkbox" id="confirmed"><span>Confirmed uplinks</span></div>

      <div>
        <label>Custom payload</label>
        <input id="customPayload" placeholder="hi and im Dhaanes">
      </div>

      <div>
        <div class="section-title">Health ping interval</div>
        <div class="interval-grid">
          <div><div class="lbl">Sec</div><input id="intervalSec" type="number" value="0"></div>
          <div><div class="lbl">Min</div><input id="intervalMin" type="number" value="0"></div>
          <div><div class="lbl">Hour</div><input id="intervalHour" type="number" value="0"></div>
          <div><div class="lbl">Day</div><input id="intervalDay" type="number" value="0"></div>
        </div>
      </div>

      <div class="btn-row">
        <button class="btn btn-save" onclick="save()">Save</button>
        <button class="btn btn-join" onclick="join()">Join</button>
        <button class="btn btn-leave" onclick="leave()">Leave</button>
        <button class="btn" onclick="forceUplink()" style="background:#d97706;color:#fff;">Force Uplink</button>
      </div>
    </div>
  </div>

  <!-- DOWNLINK STATE MONITOR (NEW BOX) -->
  <div class="card">
    <h2>📡 Downlink State Monitor</h2>
    <div id="dlStateBox">
      <div class="dl-row"><span class="dl-label">Uplink:</span><span class="dl-value" id="uplinkState">Not sent yet</span></div>
      <div class="dl-row"><span class="dl-label">Downlink Window:</span><span class="dl-value" id="dlWindowState">Closed</span></div>
      <div class="dl-row"><span class="dl-label">Next uplink in:</span><span class="dl-value dl-highlight" id="nextUplinkTimer">--</span></div>
      <div class="dl-row"><span class="dl-label">Time since uplink:</span><span class="dl-value" id="timeSinceUplink">--</span></div>
      <div class="dl-row"><span class="dl-label">Downlink status:</span><span class="dl-value" id="dlStatus">Waiting for uplink</span></div>
    </div>
  </div>

  <!-- Live log -->
  <div class="card">
    <h2>Live log</h2>
    <div id="log">Waiting for events...</div>
  </div>

  <!-- Downlink history -->
  <div class="card">
    <h2>Downlink history</h2>
    <div id="downlinkBox"><span style="color:#9ca3af;">No downlink yet</span></div>
  </div>

</div>
</div>

<script>
function doLogin(){
  if(document.getElementById("pass").value==="2240624"){
    document.getElementById("loginPage").style.display="none";
    document.getElementById("mainPage").style.display="block";
    loadCfg();
    startPolling();
  }else{
    document.getElementById("err").textContent="Incorrect password.";
  }
}

function toggle(){
  const isOTAA=document.getElementById("mode").value==="OTAA";
  ["f_joinEUI","f_devEUI","f_appKey","f_nwkKey"].forEach(id=>{
    document.getElementById(id).style.display=isOTAA?"block":"none";
  });
  ["f_devAddr","f_nwkSKey","f_appSKey"].forEach(id=>{
    document.getElementById(id).style.display=isOTAA?"none":"block";
  });
}
document.getElementById("mode").addEventListener("change",toggle);

async function loadCfg(){
  const r=await fetch('/api/config');const j=await r.json();
  document.getElementById("mode").value=j.mode||"OTAA";
  document.getElementById("joinEUI").value=j.joinEUI||"";
  document.getElementById("devEUI").value=j.devEUI||"";
  document.getElementById("appKey").value=j.appKey||"";
  document.getElementById("nwkKey").value=j.nwkKey||"";
  document.getElementById("devAddr").value=j.devAddr||"";
  document.getElementById("nwkSKey").value=j.nwkSKey||"";
  document.getElementById("appSKey").value=j.appSKey||"";
  document.getElementById("region").value=j.region||"EU868";
  document.getElementById("adr").checked=!!j.adr;
  document.getElementById("confirmed").checked=!!j.confirmed;
  document.getElementById("fPort").value=j.fPort||1;
  document.getElementById("txPower").value=j.txPower||14;
  document.getElementById("customPayload").value=j.customPayload||"";
  document.getElementById("intervalSec").value=j.intervalSec||0;
  document.getElementById("intervalMin").value=j.intervalMin||0;
  document.getElementById("intervalHour").value=j.intervalHour||0;
  document.getElementById("intervalDay").value=j.intervalDay||0;
  toggle();
}

async function save(){
  const body=JSON.stringify({
    mode:document.getElementById("mode").value,
    joinEUI:document.getElementById("joinEUI").value.trim(),
    devEUI:document.getElementById("devEUI").value.trim(),
    appKey:document.getElementById("appKey").value.trim(),
    nwkKey:document.getElementById("nwkKey").value.trim(),
    devAddr:document.getElementById("devAddr").value.trim(),
    nwkSKey:document.getElementById("nwkSKey").value.trim(),
    appSKey:document.getElementById("appSKey").value.trim(),
    region:document.getElementById("region").value,
    adr:document.getElementById("adr").checked,
    confirmed:document.getElementById("confirmed").checked,
    fPort:parseInt(document.getElementById("fPort").value)||1,
    txPower:parseInt(document.getElementById("txPower").value)||14,
    customPayload:document.getElementById("customPayload").value,
    intervalSec:parseInt(document.getElementById("intervalSec").value)||0,
    intervalMin:parseInt(document.getElementById("intervalMin").value)||0,
    intervalHour:parseInt(document.getElementById("intervalHour").value)||0,
    intervalDay:parseInt(document.getElementById("intervalDay").value)||0
  });
  await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body});
}

async function join(){await fetch('/api/join',{method:'POST'});}
async function leave(){await fetch('/api/leave',{method:'POST'});}
async function forceUplink(){
  await fetch('/api/forceuplink',{method:'POST'});
  alert('Force uplink triggered!');
}

function formatTime(sec){
  if(sec===null||sec===undefined)return'--';
  if(sec<60)return sec+'s';
  if(sec<3600)return Math.floor(sec/60)+'m '+Math.floor(sec%60)+'s';
  return Math.floor(sec/3600)+'h '+Math.floor((sec%3600)/60)+'m';
}

let pollInterval;
function startPolling(){
  if(pollInterval)clearInterval(pollInterval);
  pollInterval=setInterval(async()=>{
    try{
      const rState=await fetch('/api/dlstate');
      const state=await rState.json();
      
      if(state.uplinkSent){
        document.getElementById("uplinkState").innerHTML=`✓ Sent (${state.uplinkPayloadSize}B, port ${state.uplinkFPort})<br><small>${new Date(state.uplinkTimestamp).toLocaleTimeString()}</small>`;
        document.getElementById("dlWindowState").innerHTML='<span style="color:#059669;">✓ OPEN - TTN can schedule downlink</span>';
        if(state.downlinkReceived){
          const info=state.hasAppPayload?`Received ${state.downlinkPayloadSize}B on port ${state.downlinkFPort}`:'MAC command only';
          document.getElementById("dlStatus").innerHTML=info;
        }else{
          document.getElementById("dlStatus").innerHTML='<span style="color:#d97706;">Waiting for TTN scheduling...</span>';
        }
      }
      
      document.getElementById("nextUplinkTimer").textContent=formatTime(state.nextUplinkSec);
      document.getElementById("timeSinceUplink").textContent=formatTime(state.timeSinceUplinkSec);
      
      const rLog=await fetch('/api/logs');
      const jLog=await rLog.json();
      const box=document.getElementById("log");
      box.innerHTML="";
      for(const e of jLog.logs){
        const line=`[${e.t}][${e.d}] ${e.m}`;
        const span=document.createElement('div');
        if(e.d==='UP')span.className='log-up';
        else if(e.d==='DOWN')span.className='log-down';
        else if(e.d==='ERROR')span.className='log-err';
        span.textContent=line;
        box.appendChild(span);
      }
      box.scrollTop=box.scrollHeight;
      
      const rDl=await fetch('/api/downlink');
      const jDl=await rDl.json();
      const dlBox=document.getElementById("downlinkBox");
      if(!jDl.downlink||jDl.downlink.length===0){
        dlBox.innerHTML='<span style="color:#9ca3af;">No downlink yet</span>';
      }else{
        dlBox.innerHTML="";
        for(const e of [...jDl.downlink].reverse()){
          const row=document.createElement("div");
          row.style.padding='4px 0';
          row.style.borderBottom='1px solid #e0e4ec';
          row.innerHTML=`<span style="color:#9ca3af;">${e.t}</span> <span class="dl-port">port ${e.fport}</span> <span style="color:#4f6ef7;">${e.hex||'(empty)'}</span> <span style="color:#6b7280;">${e.ascii||''}</span>`;
          dlBox.appendChild(row);
        }
      }
      
      if(jLog.joined){
        document.getElementById("statusPill").className="status-pill pill-ok";
        document.getElementById("statusTxt").textContent="Joined";
      }
    }catch(e){}
  },1000);
}
</script>
</body></html>
)HTML";

void handleRoot() {
  server.send(200, "text/html", PAGE);
}

void handleGetConfig() {
  DynamicJsonDocument doc(2048);
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
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  DynamicJsonDocument doc(4096);
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
  cfg.intervalSec = doc["intervalSec"] | 0;
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

void handleGetLogs() {
  const int kMaxLines = 120;
  DynamicJsonDocument doc(24576);
  JsonArray arr = doc.createNestedArray("logs");
  int n = logCount < kMaxLines ? logCount : kMaxLines;
  int start = logCount >= MAX_LOG ? (logHead - n + MAX_LOG) % MAX_LOG : 0;
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % MAX_LOG;
    JsonObject o = arr.createNestedObject();
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

void handleGetDownlink() {
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.createNestedArray("downlink");
  int n = downlinkCount < MAX_DOWNLINK ? downlinkCount : MAX_DOWNLINK;
  int start = downlinkCount >= MAX_DOWNLINK ? (downlinkHead - n + MAX_DOWNLINK) % MAX_DOWNLINK : 0;
  for (int i = 0; i < n; i++) {
    int idx = (start + i) % MAX_DOWNLINK;
    JsonObject o = arr.createNestedObject();
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
  DynamicJsonDocument doc(1024);
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
      if (ENABLE_PICO_UART) picoUart.println(asc);
      Serial.print("[PICO] Sent via UART1 (force): ");
      Serial.println(asc);
      
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

// ─── setup / loop ────────────────────────────────────────────────────────────
void setup() {
  // Safe boot: start serial first and use defaults to avoid early boot loops.
  Serial.begin(115200);
  loadConfigDefaults();
  Serial.println("[BOOT] Safe defaults loaded");
  // --- ADDED FOR PICO FORWARDING: initialise UART0 (pins J6-7/8) at 115200 ---
  if (ENABLE_PICO_UART) {
    picoUart.begin(115200, SERIAL_8N1, PICO_UART_RX_PIN, PICO_UART_TX_PIN);
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

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_SN);
  bool apStarted = false;
  if (AP_PASSWORD && strlen(AP_PASSWORD) >= 8) {
    apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  } else {
    // Start open AP when password is blank/short to avoid startup failure.
    apStarted = WiFi.softAP(AP_SSID);
    addLog("WARN", "AP password empty/short: starting open AP");
  }

  if (apStarted) {
    addLog("INIT", String("AP ") + AP_SSID + " → http://192.168.4.1");
  } else {
    addLog("ERROR", "AP start failed");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/downlink", HTTP_GET, handleGetDownlink);
  server.on("/api/dlstate", HTTP_GET, handleGetDlState);
  server.on("/api/join", HTTP_POST, handleJoinReq);
  server.on("/api/leave", HTTP_POST, handleLeaveReq);
  server.on("/api/forceuplink", HTTP_POST, handleForceUplink);
  server.begin();

  addLog("INFO", "Load TTN keys, Save, then Join.");
  addLog("INFO", "Auto-join on boot disabled (safe boot mode).");
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
    Serial.println(">>> DOWNLINK PAYLOAD RECEIVED - BLINKING GREEN LED <<<");
    
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
      if (ENABLE_PICO_UART) picoUart.println(asc);
      Serial.print("[PICO] Sent via UART1 (downlink): ");
      Serial.println(asc);
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
    if (ENABLE_PICO_UART) picoUart.println(asc);
    Serial.print("[PICO] Sent via UART1 (Class C): ");
    Serial.println(asc);
    
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