#include "hex_utils.h"

#include <cstring>

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
