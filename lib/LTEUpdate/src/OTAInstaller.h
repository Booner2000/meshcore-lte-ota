#pragma once
#include "FirmwareDownloader.h"

namespace lte {
// Intentionally no Update.h, esp_ota_* calls, partition handles or reboot path.
// Later implementation must use ESP-IDF and preserve the LTE capability.
class OTAInstaller {
public:
  Error beginInactivePartition(const FirmwareManifest&) { return Error::NotImplemented; }
  Error write(const uint8_t*, size_t) { return Error::NotImplemented; }
  Error verifySha256(const uint8_t[32]) { return Error::NotImplemented; }
  Error activate() { return Error::NotImplemented; }
  Error rollback() { return Error::NotImplemented; }
  void abort() {} // No resource, flash or boot metadata exists in Phase 1.
};
}
