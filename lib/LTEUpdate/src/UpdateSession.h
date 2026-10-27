#pragma once
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "LTEUpdateManager.h"
#include "EspOtaSession.h"
#include <stdio.h>
namespace lte {
class UpdateHttp {
public:
  Error failure=Error::None;
  uint32_t actionLength=0;
  virtual ~UpdateHttp()=default;
  virtual bool prepare()=0;
  virtual bool get(const char* route,int start=-1,int end=-1)=0;
  virtual bool readBody(uint32_t offset,uint8_t* data,size_t count)=0;
  virtual bool post(const char* payload)=0;
  virtual void close()=0;
  virtual uint32_t rangeSize() const { return 32768; }
};
class UpdateSession {
  UpdateHttp& http;
  Platform& platform;
  LTEUpdateManager& manager;
  Cancellation& cancel;
  char publicKey[65]={};
  EspOtaSession* installer=nullptr;
  bool installedReport=false;
  bool report(const char* firmware,const char* hash);
  bool reportWithRetries(const char* firmware,const char* hash);
  Error run(bool reportOnly);
public:
  UpdateSession(UpdateHttp& h,Platform& p,LTEUpdateManager& m,Cancellation& c,const char* key)
    :http(h),platform(p),manager(m),cancel(c){snprintf(publicKey,sizeof(publicKey),"%s",key);}
  Error download(bool reportOnly=false,EspOtaSession* ota=nullptr);
};
}
#endif
