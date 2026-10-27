#pragma once
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include <Arduino.h>
#include "UpdateEndpoint.h"
namespace lte {
struct WifiNetwork {char ssid[33]={};char password[64]={};};
class WifiConfig {
public:
  static constexpr size_t MaxJson=8192;
  WifiNetwork networks[16];
  unsigned count=0;
  uint32_t version=0;
  UpdateEndpoint management;
  char username[65]={},password[129]={};
  bool decode(const char* text);
  bool load();
  bool save(const char* text);
  bool merge(const char* response);
  bool configured() const {return count>0;}
};
// Reserved USB-only prefix. Never forwarded to the radio CLI or echoed.
bool pollWifiProvisioning(bool busy);
void startWifiProvisioning(bool busy);
bool wifiProvisioningActive();
}
#endif
