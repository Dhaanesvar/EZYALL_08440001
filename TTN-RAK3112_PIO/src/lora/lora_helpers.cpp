#include "lora_helpers.h"

#include <cstring>

uint32_t defaultHzForRegion(const char* reg) {
  if (!strcmp(reg, "EU868")) return 868100000UL;
  if (!strcmp(reg, "EU433")) return 433175000UL;
  if (!strcmp(reg, "US915")) return 902300000UL;
  if (!strcmp(reg, "AU915")) return 915200000UL;
  if (!strcmp(reg, "AS923")) return 923200000UL;
  if (!strcmp(reg, "AS923_2")) return 923200000UL;
  if (!strcmp(reg, "AS923_3")) return 923200000UL;
  if (!strcmp(reg, "AS923_4")) return 923200000UL;
  if (!strcmp(reg, "KR920")) return 922100000UL;
  if (!strcmp(reg, "IN865")) return 865062500UL;
  if (!strcmp(reg, "CN470")) return 470300000UL;
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
  if (!strcmp(reg, "KR920")) return &KR920;
  if (!strcmp(reg, "IN865")) return &IN865;
  if (!strcmp(reg, "CN470")) return &CN470;
  return &EU868;
}

String lwErrStr(int16_t e) {
  return String(e) + " (" + String((int)e) + ")";
}
