#pragma once
#include "A7670Transport.h"

namespace lte {
class PowerStateObserver {
public:
  virtual ~PowerStateObserver() = default;
  virtual void sleepPermissionChanged(bool allowed) = 0;
};
class ModemPower {
  Platform& platform;
  const LTEUpdateConfig& config;
  A7670Transport& transport;
  bool sleepRequested = false;
  PowerStateObserver* observer;
public:
  ModemPower(Platform& p, const LTEUpdateConfig& c, A7670Transport& t, PowerStateObserver* o = nullptr)
    : platform(p), config(c), transport(t), observer(o) {}
  Error begin();
  Error powerOn();
  Error powerOff();
  Error wakeUp(Cancellation* cancel = nullptr);
  Error setSleep(bool enable, Cancellation* cancel = nullptr);
  bool isSleepRequested() const { return sleepRequested; }
};
}
