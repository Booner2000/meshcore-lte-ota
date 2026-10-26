#pragma once
#include "LTEUpdateManager.h"

#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
namespace lte {
// ESP32 adapter. The portable manager has no dependency on MeshCore classes.
bool beginService();
bool serviceCommand(const char* command, char* reply, size_t capacity);
bool serviceBusy();
void pollService();
void serviceHealthTick();
void setStationIdentity(const uint8_t* publicKey, const char* name, uint32_t utc);
}
#endif
