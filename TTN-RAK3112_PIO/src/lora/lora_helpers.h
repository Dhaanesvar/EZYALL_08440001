#pragma once

#include <Arduino.h>
#include <RadioLib.h>

uint32_t defaultHzForRegion(const char* reg);
const LoRaWANBand_t* bandPtr(const char* reg);
String lwErrStr(int16_t e);
