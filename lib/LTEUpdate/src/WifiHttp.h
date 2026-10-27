#pragma once
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "UpdateSession.h"
#include "UpdateEndpoint.h"
#include "WifiConfig.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
namespace lte {
class WifiConnection {
  Platform& platform;Cancellation& cancel;bool owned=false,oldReconnect=false,oldSleep=true;
public:
  WifiConnection(Platform& p,Cancellation& c):platform(p),cancel(c){}
  ~WifiConnection();
  Error connect(WifiConfig& config);
  void sync(WifiConfig& config);
};
class WifiHttp : public UpdateHttp {
  UpdateEndpoint endpoint;
  Platform& platform;Cancellation& cancel;
  WiFiClientSecure client;HTTPClient http;
  char identity[192]={};uint32_t readOffset=0;
  bool begin(const char* route);
  bool response(int status,int expected);
public:
  WifiHttp(Platform& p,Cancellation& c,const UpdateEndpoint& e,const char* key,const char* name);
  bool prepare() override;
  bool get(const char* route,int start=-1,int end=-1) override;
  bool readBody(uint32_t offset,uint8_t* data,size_t count) override;
  bool post(const char* payload) override;
  uint32_t rangeSize() const override { return 0x330000; }
  void close() override {http.end();client.stop();}
};
}
#endif
