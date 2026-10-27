#pragma once
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "LTEUpdateTypes.h"
#include <esp_ota_ops.h>
namespace lte {
class EspOtaSession {
  esp_ota_handle_t handle = 0;
  const esp_partition_t* partition = nullptr;
  uint32_t length = 0, counter = 0;
  char id[25] = {}, hash[65] = {};
  bool finished = false;
public:
  ~EspOtaSession();
  Error begin(uint32_t size, uint32_t release, const char* firmware, const char* sha);
  Error write(uint32_t offset, const uint8_t* data, size_t count);
  Error finish();
  Error activate(bool sleepAllowed);
};
void otaBootBegin();
void otaHealthTick();
bool otaPendingHealth();
bool otaRestoreSleep();
void otaStatus(char* reply, size_t size);
uint32_t otaReleaseFloor();
bool otaInstalledReport(char* firmware, char* sha);
void otaReportAcknowledged();
Error otaRollbackProbe();
bool otaFastUartSafe();
extern const char OtaCapability[];
}
#endif
