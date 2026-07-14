#pragma once

#include <cstdint>

struct DeviceConfig {
  char mode[8];
  char region[16];
  uint32_t frequencyHz;
  uint8_t subBand;
  bool adr;
  bool confirmed;
  uint8_t fPort;
  int8_t txPower;
  uint32_t serialBaud;

  // OTAA
  char joinEUI[32];
  char devEUI[32];
  char appKey[64];
  char nwkKey[64];

  // ABP
  char devAddr[16];
  char nwkSKey[64];
  char appSKey[64];

  // Class C and scheduler
  char lwClass[4];
  uint8_t intervalSec;
  uint8_t intervalMin;
  uint8_t intervalHour;
  uint8_t intervalDay;
  uint8_t intervalMonth;

  // Device/AP identity fields
  char uidName[32];
  char apIp[16];
  char ledUid[16];
  char ledMac[24];
  char ledIp[16];

  char customPayload[128];
};

extern DeviceConfig cfg;
extern const char PREFS_NS[];
