#include "A7670Http.h"
#include "A7670BodyReader.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "TrustAnchor.h"
#include "LTEDeploymentConfig.h"
#include "WifiConfig.h"
#include <mbedtls/base64.h>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

namespace lte {
A7670Http::A7670Http(ByteUart& u, Platform& p, LTEUpdateManager& m, Cancellation& c,
                     const char* publicKey, const char* name, const UpdateEndpoint& server)
  : endpoint(server), uart(u), platform(p), manager(m), cancel(c) {
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

bool A7670Http::command(const char* text, uint32_t timeout, const char* find, bool* found, bool sensitive) {
  if(!sensitive && strncmp(text,"AT+HTTPPARA=\"URL\"",17) && strncmp(text,"AT+HTTPREAD",11)) platform.log(text);
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
      char log[176];snprintf(log,sizeof(log),"AT failed: %.90s -> %.55s",sensitive?"private configuration request":text,line);platform.log(log);
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
  char url[360];if(!endpoint.apiUrl(url,sizeof(url),path,identity)){failure=Error::BadConfig;return false;}
  const int n=snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"URL\",\"%s\"",url);
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

bool A7670Http::post(const char* payload) {
  char cmd[400];
  command("AT+HTTPTERM");failure=Error::None;
  if(!command("AT+HTTPINIT",60000))return false;
  initialized=true;
  if(!command("AT+HTTPPARA=\"SSLCFG\",0") || !command("AT+HTTPPARA=\"READMODE\",0"))return false;
  if (!command("AT+HTTPPARA=\"USERDATA\",\"User-Agent: MeshCoreLTE/0.2\"") || !command("AT+HTTPPARA=\"CONTENT\",\"application/json\"")) return false;
  char url[360];if(!endpoint.apiUrl(url,sizeof(url),"report",identity)){failure=Error::BadConfig;return false;}
  snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"URL\",\"%s\"",url);
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

bool A7670Http::prepare() { return prepareWithPeerVerification(false); }
bool A7670Http::prepareWithPeerVerification(bool verifyPeer) {
  manager.reportProgress(State::REGISTER,5);
  bool simReady=false;
  if (!command("ATE0") || !command("AT+CMEE=2") || !command("AT+CPIN?",5000,"+CPIN: READY",&simReady))return false;
  if(!simReady){failure=Error::NotReady;return false;}
  primaryApn=false;
  if(!command("AT+CGDCONT?"))return false;
  if(!primaryApn && !command("AT+CGDCONT=1,\"IP\",\"" LTE_MODEM_APN "\""))return false;
  packetAttached=false;networkRegistered=false;
  if(!command("AT+CEREG?") || !command("AT+CGATT?"))return false;
  if(!packetAttached){
    if(!networkRegistered){platform.log("Packet service not ready; no forced reattach");{failure=Error::NotReady;return false;}}
    if(!command("AT+CGATT=1",60000))return false;
  }
  if(verifyPeer){
  const time_t utc=platform.currentUtc();
  if(utc<1767225600 || utc>2524608000) { platform.log("Set a valid MeshCore clock before TLS");{failure=Error::BadConfig;return false;} }
  struct tm time={};gmtime_r(&utc,&time);
  char clock[64];snprintf(clock,sizeof(clock),"AT+CCLK=\"%02d/%02d/%02d,%02d:%02d:%02d+00\"",(time.tm_year+1900)%100,time.tm_mon+1,time.tm_mday,time.tm_hour,time.tm_min,time.tm_sec);
  if(!command(clock) || !command("AT+CCLK?"))return false;
  if (!certificate()) return false;
  const char* tls[]={"AT+CSSLCFG=\"sslversion\",0,3", "AT+CSSLCFG=\"authmode\",0,1",
    "AT+CSSLCFG=\"ignorelocaltime\",0,0", "AT+CSSLCFG=\"enableSNI\",0,1",
    "AT+CSSLCFG=\"ignorecertCN\",0,0", "AT+CSSLCFG=\"cacert\",0,\"update-root.pem\""};
  for(const char* cmd:tls) if(!command(cmd)) {failure=Error::TlsConfig;return false;}
  }else{
    const char* tls[]={"AT+CSSLCFG=\"sslversion\",0,3", "AT+CSSLCFG=\"authmode\",0,0",
      "AT+CSSLCFG=\"ignorelocaltime\",0,1", "AT+CSSLCFG=\"enableSNI\",0,1",
      "AT+CSSLCFG=\"ignorecertCN\",0,1"};
    for(const char* cmd:tls)if(!command(cmd)){failure=Error::TlsConfig;return false;}
  }
  command("AT+HTTPTERM");failure=Error::None;
  if(!command("AT+HTTPINIT",60000))return false;
  initialized=true;
  if(!command("AT+HTTPPARA=\"SSLCFG\",0") || !command("AT+HTTPPARA=\"READMODE\",0"))return false;

  return true;
}
void A7670Http::close() {
  // Ignore cancellation during bounded cleanup; always terminate HTTP context.
  if(initialized){
    uart.discardInput();uart.write("AT+HTTPTERM\r",12);
    const uint32_t start=platform.nowMs();
    while(platform.nowMs()-start<5000){
      if(!receiveLine(1000,false))break;
      if(!strcmp(line,"OK") || !strcmp(line,"ERROR"))break;
    }
  }
  initialized=false;
}
Error A7670Http::refreshWifiConfig(WifiConfig& config){
  if(!config.username[0])return Error::NotImplemented;
  // Never send WLAN management credentials over the unverified update context.
  if(!prepareWithPeerVerification(true)){Error result=failure;close();return result;}
  char pair[196]={},encoded[272]={},cmd[400]={};size_t encodedSize=0;
  snprintf(pair,sizeof(pair),"%s:%s",config.username,config.password);
  if(mbedtls_base64_encode(reinterpret_cast<uint8_t*>(encoded),sizeof(encoded),&encodedSize,reinterpret_cast<const uint8_t*>(pair),strlen(pair))!=0){close();return Error::BadConfig;}
  snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"USERDATA\",\"Authorization: Basic %s\"",encoded);
  memset(pair,0,sizeof(pair));memset(encoded,0,sizeof(encoded));
  bool ok=command(cmd,5000,nullptr,nullptr,true);memset(cmd,0,sizeof(cmd));
  if(ok){snprintf(cmd,sizeof(cmd),"AT+HTTPPARA=\"URL\",\"%s/device/config?version=%lu\"",config.management.value(),static_cast<unsigned long>(config.version));ok=command(cmd);}
  actionStatus=-1;actionLength=0;
  if(ok)ok=command("AT+HTTPACTION=0");
  const uint32_t started=platform.nowMs();
  while(ok && actionStatus<0){
    const uint32_t elapsed=platform.nowMs()-started;
    if(elapsed>=30000){failure=Error::AtTimeout;ok=false;break;}
    ok=receiveLine(30000-elapsed);
  }
  if(ok && (actionStatus!=200 || !actionLength || actionLength>=WifiConfig::MaxJson)){failure=Error::HttpError;ok=false;}
  if(ok){
    auto* data=new uint8_t[actionLength+1];if(!data){failure=Error::NotReady;ok=false;}
    else{
      data[actionLength]=0;
      for(uint32_t offset=0;ok && offset<actionLength;offset+=1024)ok=readBody(offset,data+offset,std::min(uint32_t(1024),actionLength-offset));
      if(ok && !config.merge(reinterpret_cast<const char*>(data))){failure=Error::BadResponse;ok=false;}
      memset(data,0,actionLength+1);delete[] data;
    }
  }
  Error result=ok?Error::None:failure;close();return result;
}
}
#endif
