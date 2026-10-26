#pragma once
#include "LTEUpdatePlatform.h"

namespace lte {
class A7670Transport {
  ByteUart& uart;
  Platform& platform;
  const LTEUpdateConfig& config;
  bool started = false;
  bool transacting = false;
  bool needsResync = false;
public:
  A7670Transport(ByteUart& u, Platform& p, const LTEUpdateConfig& c)
    : uart(u), platform(p), config(c) {}
  Error begin();
  void end();
  bool active() const { return transacting; }
  // prefix=nullptr: ignore all data (ATI); prefix="": first plain line (model).
  AtResponse command(const char* command, uint32_t timeoutMs,
                     Cancellation* cancel = nullptr, const char* prefix = nullptr);
};
}
