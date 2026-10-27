#include "UpdateSession.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
namespace lte {
namespace { struct VerifiedDownload { char key[65]={};char id[25]={};char hash[65]={}; } verified; }
bool UpdateSession::report(const char* firmware,const char* hash) {
  char payload[320];snprintf(payload,sizeof(payload),"{\"firmware_id\":\"%s\",\"status\":\"%s\",\"sha256\":\"%s\",\"detail\":\"%s\"}",firmware,installedReport?"installed":"verified",hash,installedReport?"Boot health confirmed":"SHA256 verified");
  return http.post(payload);
}
bool UpdateSession::reportWithRetries(const char* id, const char* hash){
  for(unsigned attempt=0;attempt<3;++attempt){
    http.failure=Error::None;
    if(report(id,hash))return true;
    if(http.failure==Error::Cancelled)break;
    if(attempt<2){platform.log("Retrying verification report only");platform.waitMs(500);}
  }
  return false;
}

Error UpdateSession::run(bool reportOnly) {
  if(reportOnly && otaInstalledReport(verified.id,verified.hash)) {
    memcpy(verified.key,publicKey,64);verified.key[64]=0;installedReport=true;
  }
  if(reportOnly && (!verified.id[0] || strcmp(verified.key,publicKey)))return Error::NotReady;
  if(!http.prepare())return http.failure;
  manager.reportProgress(State::DATA_READY,10);
  if(reportOnly){
    manager.reportProgress(State::VERIFYING,95);
    if(!reportWithRetries(verified.id,verified.hash))return http.failure;
    if(installedReport)otaReportAcknowledged();
    manager.reportProgress(State::VERIFYING,100);return Error::None;
  }
  if(!http.get("manifest"))return http.failure;
  if(http.actionLength>=2048)return Error::BadResponse;
  char manifest[2048]={};
  if(!http.readBody(0,reinterpret_cast<uint8_t*>(manifest),http.actionLength))return http.failure;
  cJSON* json=cJSON_Parse(manifest);
  if(!json)return Error::BadResponse;
  // Update manifests are deliberately unsigned. Identity is public, not authenticated.
  const auto string=[json](const char* key)->const char* {const cJSON* v=cJSON_GetObjectItemCaseSensitive(json,key);return cJSON_IsString(v)?v->valuestring:"";};
  char firmware[25],path[100],hash[65];
  snprintf(firmware,sizeof(firmware),"%s",string("id"));snprintf(path,sizeof(path),"%s",string("download_path"));snprintf(hash,sizeof(hash),"%s",string("sha256"));
  const cJSON* size=cJSON_GetObjectItemCaseSensitive(json,"size");
  const uint32_t total=cJSON_IsNumber(size)?size->valueint:0;
  const cJSON* releaseValue=cJSON_GetObjectItemCaseSensitive(json,"release_counter");
  const uint32_t release=cJSON_IsNumber(releaseValue) && releaseValue->valuedouble>=1 && releaseValue->valuedouble<=2147483647 && releaseValue->valuedouble==releaseValue->valueint ? releaseValue->valueint : 0;
  const bool testData=!strcmp(string("artifact_kind"),"test-data");
  const bool compatible=testData ? (!strcmp(string("target"),"download-test") && !strcmp(string("capability"),"download-test-v1")) :
    (!strcmp(string("artifact_kind"),"firmware") && !strcmp(string("target"),"xiao-s3-wio-b2b-lte-v1") && !strcmp(string("capability"),"lte-ota-v1"));
  snprintf(path,sizeof(path),"firmware/%s",firmware);
  cJSON_Delete(json);
  if(installer && (testData || !release || release<=otaReleaseFloor()))return Error::BadConfig;
  if(!compatible || strlen(firmware)!=24 || strlen(hash)!=64 || !total || total>0x330000)return Error::BadResponse;
  const auto isHex=[](const char* value)->bool {for(;*value;++value)if(!(*value>='0'&&*value<='9')&&!(*value>='a'&&*value<='f'))return false;return true;};
  if(!isHex(firmware)||!isHex(hash))return Error::BadResponse;
  if(installer){Error e=installer->begin(total,release,firmware,hash);if(e!=Error::None)return e;}
  uint8_t buffer[1024],result[32];
  char tail[80]={};bool capabilitySeen=false;
  unsigned extraRanges=0;
  const uint32_t downloadStarted=platform.nowMs();
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts_ret(&sha,0);
  Error error=Error::None;
  const uint32_t range=http.rangeSize();
  if(!range || range>0x330000){mbedtls_sha256_free(&sha);return Error::BadConfig;}
  for(uint32_t start=0;start<total && error==Error::None;start+=range){
    const uint32_t amount=std::min(range,total-start);
    manager.reportProgress(State::DOWNLOADING,10+80*start/total);
    mbedtls_sha256_context checkpoint;mbedtls_sha256_init(&checkpoint);mbedtls_sha256_clone(&checkpoint,&sha);
    for(unsigned attempt=0;attempt<3;++attempt){
      if(attempt>0 && ++extraRanges>1){error=Error::DataBudget;break;}
      if(platform.nowMs()-downloadStarted>1200000){error=Error::AtTimeout;break;}
      http.failure=Error::None;error=Error::None;
      mbedtls_sha256_clone(&sha,&checkpoint);
      if(!http.get(path,start,start+amount-1) || http.actionLength!=amount) error=http.failure==Error::None?Error::BadResponse:http.failure;
      else for(uint32_t offset=0;offset<amount;offset+=sizeof(buffer)){
        const size_t count=std::min(size_t(amount-offset),sizeof(buffer));
        if(!http.readBody(offset,buffer,count)){
          char log[100];snprintf(log,sizeof(log),"HTTPREAD failed at %lu+%lu",static_cast<unsigned long>(start),static_cast<unsigned long>(offset));platform.log(log);
          error=http.failure;break;
        }
        if(installer){
          error=installer->write(start+offset,buffer,count);
          if(error!=Error::None)break;
          // Search across chunk boundaries without treating binary data as a C string.
          const size_t markerSize=strlen(OtaCapability), tailSize=start+offset?markerSize-1:0;
          uint8_t window[1104];memcpy(window,tail,tailSize);memcpy(window+tailSize,buffer,count);
          for(size_t i=0;i+markerSize<=tailSize+count;++i)if(!memcmp(window+i,OtaCapability,markerSize))capabilitySeen=true;
          memcpy(tail,window+tailSize+count-(markerSize-1),markerSize-1);
        }
        mbedtls_sha256_update_ret(&sha,buffer,count);
      }
      if(error==Error::FlashError || error==Error::DataBudget || error==Error::None || error==Error::Cancelled || error==Error::HttpBlocked || error==Error::TlsConfig || error==Error::NetworkUnavailable)break;
      if(attempt<2){platform.log("Retrying complete range; SHA256 state restored");platform.waitMs(500);}
    }
    mbedtls_sha256_free(&checkpoint);
  }
  mbedtls_sha256_finish_ret(&sha,result);mbedtls_sha256_free(&sha);
  if(error!=Error::None)return error;
  manager.reportProgress(State::VERIFYING,95);
  char actual[65];for(unsigned i=0;i<32;++i)snprintf(actual+i*2,3,"%02x",result[i]);
  if(strcmp(actual,hash))return Error::HashMismatch;
  if(installer){
    if(!capabilitySeen)return Error::BadConfig;
    Error e=installer->finish();if(e!=Error::None)return e;
    platform.log("Image verified in inactive OTA slot; not yet activated");
  }else platform.log("SHA256 verified on ESP32; image not installed");
  memcpy(verified.key,publicKey,64);verified.key[64]=0;
  snprintf(verified.id,sizeof(verified.id),"%s",firmware);snprintf(verified.hash,sizeof(verified.hash),"%s",hash);
  if(!reportWithRetries(firmware,hash))return http.failure;
  manager.reportProgress(State::VERIFYING,100);
  return Error::None;
}
Error UpdateSession::download(bool reportOnly,EspOtaSession* ota){
  installer=ota;Error result=run(reportOnly);http.close();return result;
}
}
#endif
