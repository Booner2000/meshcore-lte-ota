#include "A7670Http.h"
#include "A7670BodyReader.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "TrustAnchor.h"
#include "LTEDeploymentConfig.h"
#include "TrustedKey.h"
#include <Ed25519.h>
#include <mbedtls/sha256.h>
#include <cJSON.h>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

namespace lte {
namespace {
// Only the single worker accesses this cache. It never implies installation.
struct VerifiedDownload { char key[65]={};char id[25]={};char hash[65]={}; } verified;
}
A7670Http::A7670Http(ByteUart& u, Platform& p, LTEUpdateManager& m, Cancellation& c,
                     const char* publicKey, const char* name)
  : uart(u), platform(p), manager(m), cancel(c) {
  char encoded[100] = {};
  size_t used = 0;
  const char hex[] = "0123456789ABCDEF";
  for (const unsigned char* s = reinterpret_cast<const unsigned char*>(name); *s && used+3 < sizeof(encoded); ++s) {
    if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_') encoded[used++] = *s;
    else { encoded[used++] = '%'; encoded[used++] = hex[*s>>4]; encoded[used++] = hex[*s&15]; }
  }
  snprintf(identity, sizeof(identity), "?key=%s&name=%s", publicKey, encoded);
}

bool A7670Http::receiveLine(uint32_t timeout, bool cancellable) {
  // Saturate the deadline subtraction if preemption crosses its boundary.
  if(timeout>120000)timeout=120000;
  const uint32_t start = platform.nowMs();
  size_t used = 0;
  while (platform.nowMs()-start < timeout) {
    if (cancellable && cancel.cancelled()) { failure = Error::Cancelled; return false; }
    if (uart.fault() != Error::None) { failure = uart.fault(); return false; }
    char c;
    if (!uart.read(c, 10)) continue;
    if (c == '\r') continue;
    if (c == '\n') {
      if (!used) continue;
      line[used] = 0;
      unsigned method, code, count;
      if (sscanf(line, "+HTTPACTION: %u,%u,%u", &method, &code, &count) == 3) {
        actionStatus = code; actionLength = count;
      }
      if(!strncmp(line,"+CGDCONT: 1,",12) && strstr(line,"\"" LTE_MODEM_APN "\""))primaryApn=true;
      int attached,mode,registration;
      if(sscanf(line,"+CGATT: %d",&attached)==1)packetAttached=attached==1;
      if(sscanf(line,"+CEREG: %d,%d",&mode,&registration)==2)networkRegistered=registration==1 || registration==5;
      return true;
    }
    if (used+1 >= sizeof(line)) { failure = Error::ResponseOverflow; return false; }
    line[used++] = c;
  }
  failure = Error::AtTimeout;
  return false;
}

bool A7670Http::command(const char* text, uint32_t timeout, const char* find, bool* found) {
  if(strncmp(text,"AT+HTTPPARA=\"URL\"",17) && strncmp(text,"AT+HTTPREAD",11)) platform.log(text);
  if (found) *found = false;
  uart.discardInput();
  if (!uart.write(text, strlen(text)) || !uart.write("\r\n", 2)) { failure = Error::UartIo; return false; }
  const uint32_t start = platform.nowMs();
  while (platform.nowMs()-start < timeout) {
    if (!receiveLine(timeout-(platform.nowMs()-start))) return false;
    if (strcmp(line,"OK") && strncmp(line,"AT",2)) platform.log(line);
    if (find && strstr(line, find) && found) *found = true;
    if (!strcmp(line,"OK")) return true;
    if (!strcmp(line,"ERROR") || !strncmp(line,"+CME ERROR",10)) {
      char log[176];snprintf(log,sizeof(log),"AT failed: %.90s -> %.55s",text,line);platform.log(log);
      failure = Error::AtError;return false;
    }
  }
  failure = Error::AtTimeout;return false;
}

bool A7670Http::certificate() {
  bool exists = false;
  if (!command("AT+CCERTDOWN=?")) return false;
  if (!command("AT+CCERTLIST", 5000, "update-root.pem", &exists)) return false;
  if (exists) return true;
  char cmd[80];snprintf(cmd,sizeof(cmd),"AT+CCERTDOWN=\"update-root.pem\",%u",unsigned(strlen(RootCa)));
  uart.discardInput();uart.write(cmd,strlen(cmd));uart.write("\r\n",2);
  const uint32_t start = platform.nowMs();bool prompt=false;
  char response[96]={};size_t received=0;
  while (platform.nowMs()-start < 5000) {
    char c;if (uart.read(c,10)) {
      if(received+1<sizeof(response)) {response[received++]=c;response[received]=0;}
      if(c=='>' || strstr(response,"DOWNLOAD")) {prompt=true;break;}
    }
    if (cancel.cancelled()) {failure=Error::Cancelled;return false;}
  }
  if (!prompt) {platform.log("Certificate upload prompt missing");platform.log(response);failure=Error::AtTimeout;return false;}
  if (!uart.write(RootCa,strlen(RootCa))) {failure=Error::UartIo;return false;}
  while (platform.nowMs()-start < 15000) {
    if (!receiveLine(5000)) return false;
    if (!strcmp(line,"OK")) return true;
    if (!strcmp(line,"ERROR")) {failure=Error::AtError;return false;}
  }
  failure=Error::AtTimeout;return false;
}

bool A7670Http::get(const char* path, int rangeStart, int rangeEnd) {
  char cmd[400];
  // This A7670E requires a fresh HTTP context for each independently ranged GET.
  command("AT+HTTPTERM");failure=Error::None;
  if(!command("AT+HTTPINIT",60000))return false;
  initialized=true;
  if(!command("AT+HTTPPARA=\"SSLCFG\",0") || !command("AT+HTTPPARA=\"READMODE\",0"))return false;
  if (rangeStart >= 0) snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"USERDATA\",\"Range: bytes=%d-%d\"",rangeStart,rangeEnd);
  else snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"USERDATA\",\"User-Agent: MeshCoreLTE/0.2\"");
  if (!command(cmd)) return false;
  const int n=snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"URL\",\"" LTE_UPDATE_ORIGIN "%s%s\"",path,identity);
  if (n<0 || n>=int(sizeof(cmd))) {failure=Error::BadConfig;return false;}
  if (!command(cmd)) return false;
  actionStatus=-1;actionLength=0;
  if (!command("AT+HTTPACTION=0")) return false;
  const uint32_t start=platform.nowMs();
  while (actionStatus < 0 && platform.nowMs()-start < 120000) if (!receiveLine(120000-(platform.nowMs()-start))) return false;
  char log[96];snprintf(log,sizeof(log),"HTTPS status=%d bytes=%lu",actionStatus,static_cast<unsigned long>(actionLength));platform.log(log);
  if (actionStatus==403) {failure=Error::HttpBlocked;return false;}
  if (actionStatus!=(rangeStart>=0?206:200)) {failure=Error::HttpError;return false;}
  if (actionLength==0 || actionLength>65536) {failure=Error::BadResponse;return false;}
  return true;
}

bool A7670Http::readBody(uint32_t offset,uint8_t* data,size_t count){
  failure=readBufferedHttpBody(uart,platform,cancel,offset,data,count);
  return failure==Error::None;
}

bool A7670Http::report(const char* firmware, const char* hash) {
  char payload[320];snprintf(payload,sizeof(payload),"{\"firmware_id\":\"%s\",\"status\":\"%s\",\"sha256\":\"%s\",\"detail\":\"%s\"}",firmware,installedReport?"installed":"verified",hash,installedReport?"Boot health confirmed":"SHA256 verified");
  char cmd[400];
  command("AT+HTTPTERM");failure=Error::None;
  if(!command("AT+HTTPINIT",60000))return false;
  initialized=true;
  if(!command("AT+HTTPPARA=\"SSLCFG\",0") || !command("AT+HTTPPARA=\"READMODE\",0"))return false;
  if (!command("AT+HTTPPARA=\"USERDATA\",\"User-Agent: MeshCoreLTE/0.2\"") || !command("AT+HTTPPARA=\"CONTENT\",\"application/json\"")) return false;
  snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"URL\",\"" LTE_UPDATE_ORIGIN "/meshcoreupdate/api/v1/report%s\"",identity);
  if (!command(cmd)) return false;
  snprintf(cmd,sizeof(cmd),"AT+HTTPDATA=%u,10000",unsigned(strlen(payload)));
  uart.discardInput();uart.write(cmd,strlen(cmd));uart.write("\r\n",2);
  if (!receiveLine(5000) || strcmp(line,"DOWNLOAD")) {failure=Error::BadResponse;return false;}
  uart.write(payload,strlen(payload));
  if (!receiveLine(5000) || strcmp(line,"OK")) {failure=Error::BadResponse;return false;}
  actionStatus=-1;
  if (!command("AT+HTTPACTION=1")) return false;
  const uint32_t start=platform.nowMs();
  while(actionStatus<0 && platform.nowMs()-start<120000) if(!receiveLine(120000-(platform.nowMs()-start)))return false;
  char log[64];snprintf(log,sizeof(log),"HTTPS report status=%d",actionStatus);platform.log(log);
  if(actionStatus!=200){failure=Error::HttpError;return false;}
  return true;
}

bool A7670Http::reportWithRetries(const char* id, const char* hash){
  for(unsigned attempt=0;attempt<3;++attempt){
    failure=Error::None;
    if(report(id,hash))return true;
    if(failure==Error::Cancelled)break;
    if(attempt<2){platform.log("Retrying verification report only");platform.waitMs(500);}
  }
  return false;
}

Error A7670Http::run(bool reportOnly) {
  if(reportOnly && otaInstalledReport(verified.id,verified.hash)) {
    memcpy(verified.key,identity+5,64);verified.key[64]=0;installedReport=true;
  }
  if(reportOnly && (!verified.id[0] || strncmp(verified.key,identity+5,64)))return Error::NotReady;
  manager.reportProgress(State::REGISTER,5);
  bool simReady=false;
  if (!command("ATE0") || !command("AT+CMEE=2") || !command("AT+CPIN?",5000,"+CPIN: READY",&simReady))return failure;
  if(!simReady)return Error::NotReady;
  primaryApn=false;
  if(!command("AT+CGDCONT?"))return failure;
  if(!primaryApn && !command("AT+CGDCONT=1,\"IP\",\"" LTE_MODEM_APN "\""))return failure;
  packetAttached=false;networkRegistered=false;
  if(!command("AT+CEREG?") || !command("AT+CGATT?"))return failure;
  if(!packetAttached){
    if(!networkRegistered){platform.log("Packet service not ready; no forced reattach");return Error::NotReady;}
    if(!command("AT+CGATT=1",60000))return failure;
  }
  const time_t utc=platform.currentUtc();
  if(utc<1767225600 || utc>2524608000) { platform.log("Set a valid MeshCore clock before TLS");return Error::BadConfig; }
  struct tm time={};gmtime_r(&utc,&time);
  char clock[64];snprintf(clock,sizeof(clock),"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d+00\"",(time.tm_year+1900)%100,time.tm_mon+1,time.tm_mday,time.tm_hour,time.tm_min,time.tm_sec);
  if(!command(clock) || !command("AT+CCLK?"))return failure;
  if (!certificate()) return failure;
  const char* tls[]={"AT+CSSLCFG=\"sslversion\",0,3", "AT+CSSLCFG=\"authmode\",0,1",
    "AT+CSSLCFG=\"ignorelocaltime\",0,0", "AT+CSSLCFG=\"enableSNI\",0,1",
    "AT+CSSLCFG=\"ignorecertCN\",0,0", "AT+CSSLCFG=\"cacert\",0,\"update-root.pem\""};
  for(const char* cmd:tls) if(!command(cmd)) return Error::TlsConfig;
  command("AT+HTTPTERM");failure=Error::None;
  if(!command("AT+HTTPINIT",60000))return failure;
  initialized=true;
  if(!command("AT+HTTPPARA=\"SSLCFG\",0") || !command("AT+HTTPPARA=\"READMODE\",0"))return failure;
  manager.reportProgress(State::DATA_READY,10);
  if(reportOnly){
    manager.reportProgress(State::VERIFYING,95);
    if(!reportWithRetries(verified.id,verified.hash))return failure;
    if(installedReport)otaReportAcknowledged();
    manager.reportProgress(State::VERIFYING,100);return Error::None;
  }
  if(!get("/meshcoreupdate/api/v1/manifest"))return failure;
  if(actionLength>=2048)return Error::BadResponse;
  char manifest[2048]={};
  if(!readBody(0,reinterpret_cast<uint8_t*>(manifest),actionLength))return failure;
  cJSON* json=cJSON_Parse(manifest);
  if(!json)return Error::BadResponse;
  if(installer){
    const cJSON* payload=cJSON_GetObjectItemCaseSensitive(json,"signed_payload");
    const cJSON* signature=cJSON_GetObjectItemCaseSensitive(json,"signature");
    uint8_t publicKey[32],sig[64];
    const auto decode=[](const char* text,uint8_t* out,size_t count)->bool {
      if(!text || strlen(text)!=count*2)return false;
      for(size_t i=0;i<count;++i){
        unsigned value=0;
        for(unsigned j=0;j<2;++j){char c=text[2*i+j];if(c<'0'||(c>'9'&&c<'a')||c>'f')return false;value=value*16+(c<='9'?c-'0':c-'a'+10);}
        out[i]=value;
      }return true;
    };
    if(!cJSON_IsString(payload) || !cJSON_IsString(signature) || strlen(payload->valuestring)>1024 ||
      !decode(LTE_SIGNING_PUBLIC_KEY_HEX,publicKey,32) || !decode(signature->valuestring,sig,64) ||
      !Ed25519::verify(sig,publicKey,payload->valuestring,strlen(payload->valuestring))){cJSON_Delete(json);return Error::SignatureInvalid;}
    cJSON* signedJson=cJSON_Parse(payload->valuestring);cJSON_Delete(json);json=signedJson;
    if(!json)return Error::BadResponse;
  }
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
  if(installer)snprintf(path,sizeof(path),"/meshcoreupdate/api/v1/firmware/%s",firmware);
  cJSON_Delete(json);
  if(installer && (testData || !release || release<=otaReleaseFloor()))return Error::BadConfig;
  if(!compatible || strlen(firmware)!=24 || strlen(hash)!=64 || strncmp(path,"/meshcoreupdate/api/v1/firmware/",30) || !total || total>0x330000)return Error::BadResponse;
  if(installer){Error e=installer->begin(total,release,firmware,hash);if(e!=Error::None)return e;}
  uint8_t buffer[1024],result[32];
  char tail[80]={};bool capabilitySeen=false;
  unsigned extraRanges=0;
  const uint32_t downloadStarted=platform.nowMs();
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts_ret(&sha,0);
  Error error=Error::None;
  for(uint32_t start=0;start<total && error==Error::None;start+=32768){
    const uint32_t amount=std::min(uint32_t(32768),total-start);
    manager.reportProgress(State::DOWNLOADING,10+80*start/total);
    mbedtls_sha256_context checkpoint;mbedtls_sha256_init(&checkpoint);mbedtls_sha256_clone(&checkpoint,&sha);
    for(unsigned attempt=0;attempt<3;++attempt){
      if(attempt>0 && ++extraRanges>1){error=Error::DataBudget;break;}
      if(platform.nowMs()-downloadStarted>1200000){error=Error::AtTimeout;break;}
      failure=Error::None;error=Error::None;
      mbedtls_sha256_clone(&sha,&checkpoint);
      if(!get(path,start,start+amount-1) || actionLength!=amount) error=failure==Error::None?Error::BadResponse:failure;
      else for(uint32_t offset=0;offset<amount;offset+=sizeof(buffer)){
        const size_t count=std::min(size_t(amount-offset),sizeof(buffer));
        if(!readBody(offset,buffer,count)){
          char log[100];snprintf(log,sizeof(log),"HTTPREAD failed at %lu+%lu",static_cast<unsigned long>(start),static_cast<unsigned long>(offset));platform.log(log);
          error=failure;break;
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
      if(error==Error::FlashError || error==Error::DataBudget || error==Error::None || error==Error::Cancelled || error==Error::HttpBlocked || error==Error::TlsConfig)break;
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
    platform.log("Signed image verified in inactive OTA slot; not yet activated");
  }else platform.log("SHA256 verified on ESP32; image not installed");
  memcpy(verified.key,identity+5,64);verified.key[64]=0;
  snprintf(verified.id,sizeof(verified.id),"%s",firmware);snprintf(verified.hash,sizeof(verified.hash),"%s",hash);
  if(!reportWithRetries(firmware,hash))return failure;
  manager.reportProgress(State::VERIFYING,100);
  return Error::None;
}

Error A7670Http::download(bool reportOnly,EspOtaSession* ota){
  installer=ota;
  Error result=run(reportOnly);
  // Ignore cancellation during bounded cleanup; always terminate HTTP context.
  if(initialized){
    uart.discardInput();uart.write("AT+HTTPTERM\r",12);
    const uint32_t start=platform.nowMs();
    while(platform.nowMs()-start<5000){
      if(!receiveLine(1000,false))break;
      if(!strcmp(line,"OK") || !strcmp(line,"ERROR"))break;
    }
  }
  return result;
}
}
#endif
