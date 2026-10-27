#pragma once
#include "ModemPower.h"
#include "FirmwareDownloader.h"
#include "OTAInstaller.h"

namespace lte {
class LTEUpdateManager : public Cancellation, public PowerStateObserver {
  Platform& platform;
  LTEUpdateConfig config;
  A7670Transport transport;
  ModemPower power;
  Status status;
  bool delayCancellable(uint32_t ms);
  void transition(State state, uint8_t progress);
  Error diagnose();
  Error simulate();
public:
  LTEUpdateManager(Platform& p, ByteUart& uart, const LTEUpdateConfig& c)
    : platform(p), config(c), transport(uart, p, config), power(p, config, transport, this) {}
  Error begin();
  bool handleCommand(const char* command, char* reply, size_t capacity);
  Status snapshot();
  void reportProgress(State state, uint8_t progress) { transition(state, progress); }
  void reportTransport(UpdateRoute route) {Lock guard(platform);status.activeRoute=route;}
  bool cancelled() override;
  void sleepPermissionChanged(bool allowed) override;
  // Only the single worker calls this, never the MeshCore loop/CLI.
  void run(Operation operation);
};
}
