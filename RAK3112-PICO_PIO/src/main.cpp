#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <RadioLib.h>

// RAK3112 SX1262 pins
#define LORA_SX126X_SCK     5
#define LORA_SX126X_MISO    3
#define LORA_SX126X_MOSI    6
#define LORA_SX126X_CS      7
#define LORA_SX126X_RESET   8
#define LORA_SX126X_DIO1    47
#define LORA_SX126X_BUSY    48

#define LED_RED   35
#define LED_GREEN 34

// Pico UART default pins
#define PICO_RX_PIN 33
#define PICO_TX_PIN 32
HardwareSerial PicoSerial(1);

Module sx1262Module(LORA_SX126X_CS, LORA_SX126X_DIO1, LORA_SX126X_RESET, LORA_SX126X_BUSY, SPI);
SX1262 radio(&sx1262Module);
LoRaWANNode* lorawan = nullptr;
const LoRaWANBand_t* lwBand = &EU868;

WebServer server(80);
Preferences prefs;

struct DeviceConfig {
  char mode[8];
  char region[16];
  char joinEUI[32];
  char devEUI[32];
  char appKey[64];
  char nwkKey[64];
  char devAddr[16];
  char nwkSKey[64];
  char appSKey[64];
  bool adr;
  bool confirmed;
  uint8_t fPort;
  int8_t txPower;
  char customPayload[128];
} cfg;

static const char* AP_SSID = "CRE8IOT_08440001";
static const char* AP_PASSWORD = "";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_SN(255, 255, 255, 0);

bool g_joined = false;
uint32_t g_lastUplinkMs = 0;

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

static bool parseHexString(const char* s, uint8_t* out, size_t nBytes) {
  if (!s) return false;
  char tmp[128];
  size_t j = 0;
  for (size_t i = 0; s[i] && j < sizeof(tmp) - 1; i++) {
    if (s[i] == ' ' || s[i] == ':' || s[i] == '-') continue;
    tmp[j++] = s[i];
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

static bool parseHexU64(const char* s, uint64_t* out) {
  uint8_t b[8];
  if (!parseHexString(s, b, 8)) return false;
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
  *out = v;
  return true;
}

void loadConfig() {
  prefs.begin("lwcfg", true);
  strlcpy(cfg.mode, prefs.getString("mode", "OTAA").c_str(), sizeof(cfg.mode));
  strlcpy(cfg.region, prefs.getString("region", "EU868").c_str(), sizeof(cfg.region));
  strlcpy(cfg.joinEUI, prefs.getString("joinEUI", "").c_str(), sizeof(cfg.joinEUI));
  strlcpy(cfg.devEUI, prefs.getString("devEUI", "").c_str(), sizeof(cfg.devEUI));
  strlcpy(cfg.appKey, prefs.getString("appKey", "").c_str(), sizeof(cfg.appKey));
  strlcpy(cfg.nwkKey, prefs.getString("nwkKey", "").c_str(), sizeof(cfg.nwkKey));
  strlcpy(cfg.devAddr, prefs.getString("devAddr", "").c_str(), sizeof(cfg.devAddr));
  strlcpy(cfg.nwkSKey, prefs.getString("nwkSKey", "").c_str(), sizeof(cfg.nwkSKey));
  strlcpy(cfg.appSKey, prefs.getString("appSKey", "").c_str(), sizeof(cfg.appSKey));
  cfg.adr = prefs.getBool("adr", true);
  cfg.confirmed = prefs.getBool("confirmed", false);
  cfg.fPort = prefs.getUChar("fPort", 1);
  cfg.txPower = (int8_t)prefs.getChar("txPower", 14);
  strlcpy(cfg.customPayload, prefs.getString("payload", "hi and im Dhaanes").c_str(), sizeof(cfg.customPayload));
  prefs.end();
}

void saveConfig() {
  prefs.begin("lwcfg", false);
  prefs.putString("mode", cfg.mode);
  prefs.putString("region", cfg.region);
  prefs.putString("joinEUI", cfg.joinEUI);
  prefs.putString("devEUI", cfg.devEUI);
  prefs.putString("appKey", cfg.appKey);
  prefs.putString("nwkKey", cfg.nwkKey);
  prefs.putString("devAddr", cfg.devAddr);
  prefs.putString("nwkSKey", cfg.nwkSKey);
  prefs.putString("appSKey", cfg.appSKey);
  prefs.putBool("adr", cfg.adr);
  prefs.putBool("confirmed", cfg.confirmed);
  prefs.putUChar("fPort", cfg.fPort);
  prefs.putChar("txPower", cfg.txPower);
  prefs.putString("payload", cfg.customPayload);
  prefs.end();
}

bool doJoin() {
  uint8_t appKey[16], nwkKey[16];
  uint64_t joinEUI = 0, devEUI = 0;

  if (!parseHexU64(cfg.joinEUI, &joinEUI) || !parseHexU64(cfg.devEUI, &devEUI)) {
    Serial.println("JoinEUI/DevEUI invalid");
    return false;
  }
  if (!parseHexString(cfg.appKey, appKey, 16)) {
    Serial.println("AppKey invalid");
    return false;
  }
  if (!parseHexString(cfg.nwkKey, nwkKey, 16)) memcpy(nwkKey, appKey, 16);

  SPI.begin(LORA_SX126X_SCK, LORA_SX126X_MISO, LORA_SX126X_MOSI, LORA_SX126X_CS);
  int16_t st = radio.begin(868.1f, 125.0f, 7, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, cfg.txPower, 8, 1.8f, false);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("radio.begin failed: %d\n", st);
    return false;
  }

  if (lorawan) {
    delete lorawan;
    lorawan = nullptr;
  }
  lorawan = new LoRaWANNode(&radio, lwBand, 0);
  if (!lorawan) return false;

  lorawan->beginOTAA(joinEUI, devEUI, nwkKey, appKey);

  st = lorawan->activateOTAA();
  if (st != RADIOLIB_LORAWAN_NEW_SESSION && st != RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.printf("activateOTAA failed: %d\n", st);
    return false;
  }

  lorawan->setADR(cfg.adr);
  lorawan->setTxPower(cfg.txPower);

  g_joined = true;
  Serial.println("Joined TTN");
  return true;
}

void sendPeriodicUplink() {
  if (!lorawan || !g_joined) return;
  if (millis() - g_lastUplinkMs < 60000UL) return;
  g_lastUplinkMs = millis();

  const char* payload = cfg.customPayload[0] ? cfg.customPayload : "hi and im Dhaanes";
  uint8_t up[128];
  size_t upLen = strlen(payload);
  if (upLen > sizeof(up)) upLen = sizeof(up);
  memcpy(up, payload, upLen);
  uint8_t down[256];
  size_t downLen = 0;
  int16_t st = lorawan->sendReceive(up, upLen, cfg.fPort, down, &downLen, cfg.confirmed);

  if (st >= 0 && downLen > 0) {
    String ascii;
    for (size_t i = 0; i < downLen; i++) {
      ascii += (down[i] >= 32 && down[i] <= 126) ? (char)down[i] : '.';
    }
    Serial.printf("[DOWNLINK] %s\n", ascii.c_str());
    PicoSerial.println(ascii);
    Serial.printf("[PICO] Sent: %s\n", ascii.c_str());
  }
}

void handleGetConfig() {
  DynamicJsonDocument d(2048);
  d["mode"] = cfg.mode;
  d["region"] = cfg.region;
  d["joinEUI"] = cfg.joinEUI;
  d["devEUI"] = cfg.devEUI;
  d["appKey"] = cfg.appKey;
  d["nwkKey"] = cfg.nwkKey;
  d["devAddr"] = cfg.devAddr;
  d["nwkSKey"] = cfg.nwkSKey;
  d["appSKey"] = cfg.appSKey;
  d["adr"] = cfg.adr;
  d["confirmed"] = cfg.confirmed;
  d["fPort"] = cfg.fPort;
  d["txPower"] = cfg.txPower;
  d["customPayload"] = cfg.customPayload;
  String out;
  serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handlePostConfig() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  DynamicJsonDocument d(4096);
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }

  strlcpy(cfg.mode, d["mode"] | "OTAA", sizeof(cfg.mode));
  strlcpy(cfg.region, d["region"] | "EU868", sizeof(cfg.region));
  strlcpy(cfg.joinEUI, d["joinEUI"] | "", sizeof(cfg.joinEUI));
  strlcpy(cfg.devEUI, d["devEUI"] | "", sizeof(cfg.devEUI));
  strlcpy(cfg.appKey, d["appKey"] | "", sizeof(cfg.appKey));
  strlcpy(cfg.nwkKey, d["nwkKey"] | "", sizeof(cfg.nwkKey));
  strlcpy(cfg.devAddr, d["devAddr"] | "", sizeof(cfg.devAddr));
  strlcpy(cfg.nwkSKey, d["nwkSKey"] | "", sizeof(cfg.nwkSKey));
  strlcpy(cfg.appSKey, d["appSKey"] | "", sizeof(cfg.appSKey));
  cfg.adr = d["adr"] | true;
  cfg.confirmed = d["confirmed"] | false;
  cfg.fPort = d["fPort"] | 1;
  cfg.txPower = (int8_t)(d["txPower"] | 14);
  strlcpy(cfg.customPayload, d["customPayload"] | "hi and im Dhaanes", sizeof(cfg.customPayload));

  saveConfig();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleJoin() {
  bool ok = doJoin();
  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void setup() {
  loadConfig();
  Serial.begin(115200);
  PicoSerial.begin(115200, SERIAL_8N1, PICO_RX_PIN, PICO_TX_PIN);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_GW, AP_SN);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/join", HTTP_POST, handleJoin);
  server.begin();

  Serial.println("RAK3112-Pico-PIO ready at http://192.168.4.1");
}

void loop() {
  server.handleClient();
  sendPeriodicUplink();

  if (g_joined) {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, HIGH);
  } else {
    digitalWrite(LED_GREEN, HIGH);
  }
}
