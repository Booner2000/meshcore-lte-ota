#pragma once
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "LTEUpdateManager.h"
#include "EspOtaSession.h"
#include "UpdateEndpoint.h"
#include "UpdateSession.h"

namespace lte {
class WifiConfig;
// Real LTE/HTTPS download test. Streams through a 1-KiB buffer, no flash writer.
class A7670Http : public UpdateHttp {
  UpdateEndpoint endpoint;
  ByteUart& uart;
  Platform& platform;
  LTEUpdateManager& manager;
  Cancellation& cancel;
  char identity[192] = {};
  char line[320] = {};
  int actionStatus = -1;
  bool initialized = false;
  bool primaryApn = false, packetAttached = false, networkRegistered = false;
  bool receiveLine(uint32_t timeout, bool cancellable = true);
  bool command(const char* text, uint32_t timeout = 5000, const char* find = nullptr, bool* found = nullptr, bool sensitive = false);
  bool certificate();
  bool prepareWithPeerVerification(bool verifyPeer);
  bool get(const char* path, int rangeStart = -1, int rangeEnd = -1);
  bool readBody(uint32_t offset, uint8_t* data, size_t count);
  bool post(const char* payload) override;
  bool prepare() override;
  void close() override;
public:
  A7670Http(ByteUart& u, Platform& p, LTEUpdateManager& m, Cancellation& c,
            const char* publicKey, const char* name, const UpdateEndpoint& server);
  Error refreshWifiConfig(WifiConfig& config);

};
}
#endif
