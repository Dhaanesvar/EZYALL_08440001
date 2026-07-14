#pragma once

#include <cstddef>
#include <cstdint>

bool parseHexString(const char* s, uint8_t* out, size_t nBytes);
bool parseHexU64MsbFirst(const char* s, uint64_t* out);
bool parseHexU32(const char* s, uint32_t* out);
