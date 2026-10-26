#pragma once
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "LTEUpdateManager.h"

namespace lte {
// Real LTE/HTTPS download test. Streams through a 1-KiB buffer, no flash writer.
class A7670Http {
  ByteUart& uart;
  Platform& platform;
  LTEUpdateManager& manager;
  Cancellation& cancel;
  char identity[192] = {};
  char line[320] = {};
  int actionStatus = -1;
  uint32_t actionLength = 0;
  bool initialized = false;
  bool primaryApn = false, packetAttached = false, networkRegistered = false;
  Error failure = Error::None;
  bool receiveLine(uint32_t timeout, bool cancellable = true);
  bool command(const char* text, uint32_t timeout = 5000, const char* find = nullptr, bool* found = nullptr);
  bool certificate();
  bool get(const char* path, int rangeStart = -1, int rangeEnd = -1);
  bool readBody(uint32_t offset, uint8_t* data, size_t count);
  bool report(const char* firmware, const char* hash);
  bool reportWithRetries(const char* firmware, const char* hash);
  Error run(bool reportOnly);
public:
  A7670Http(ByteUart& u, Platform& p, LTEUpdateManager& m, Cancellation& c,
            const char* publicKey, const char* name);
  Error download(bool reportOnly = false);
};
}
#endif
