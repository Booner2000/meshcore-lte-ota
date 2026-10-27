#include "WifiHttp.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include "TrustAnchor.h"
#include "WifiFailurePolicy.h"
#include <sys/time.h>
#include <mbedtls/x509.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <algorithm>
namespace lte {
namespace {
Error tlsError(WiFiClientSecure& client){
  char unused[8];int error=client.lastError(unused,sizeof(unused));
  if(error==MBEDTLS_ERR_SSL_TIMEOUT || error==MBEDTLS_ERR_NET_CONN_RESET ||
    error==MBEDTLS_ERR_NET_RECV_FAILED || error==MBEDTLS_ERR_NET_SEND_FAILED || error==MBEDTLS_ERR_NET_CONNECT_FAILED)
    return Error::NetworkUnavailable;
  return wifiConnectFailure(error); // Certificate, hostname and date failures fail closed.
}
bool setClock(Platform& p){
  uint32_t utc=p.currentUtc();if(utc<1767225600 || utc>2524608000)return false;
  timeval now={static_cast<time_t>(utc),0};return settimeofday(&now,nullptr)==0;
}
bool readExact(HTTPClient& http,Platform& p,Cancellation& cancel,uint8_t* out,size_t count){
  WiFiClient* stream=http.getStreamPtr();size_t got=0;uint32_t last=p.nowMs();
  while(got<count){
    if(cancel.cancelled())return false;
    int available=stream->available();
    if(available>0){
      int n=stream->read(out+got,std::min(size_t(available),count-got));
      if(n>0){got+=n;last=p.nowMs();continue;}
    }
    if(!WiFi.isConnected() || (!stream->connected() && !stream->available()) || p.nowMs()-last>5000)return false;
    p.waitMs(5);
  }
  return true;
}
void secure(WiFiClientSecure& client,HTTPClient& http,bool verifyPeer=true){
  // Management requests keep CA/hostname/date validation; firmware deliberately does not.
  if(verifyPeer)client.setCACert(RootCa);else client.setInsecure();
  client.setHandshakeTimeout(8);
  http.setConnectTimeout(8000);http.setTimeout(5000);http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
}
}
WifiConnection::~WifiConnection(){
  if(owned){WiFi.scanDelete();WiFi.setSleep(oldSleep);WiFi.disconnect(false,false);WiFi.mode(WIFI_OFF);WiFi.setAutoReconnect(oldReconnect);}
}
Error WifiConnection::connect(WifiConfig& config){
  if(cancel.cancelled())return Error::Cancelled;
  if(WiFi.isConnected())return Error::None; // Borrow a pre-existing STA connection.
  if(WiFi.getMode()!=WIFI_OFF)return Error::Busy; // Do not seize the manual OTA AP.
  if(!config.configured())return Error::NotImplemented;
  owned=true;oldReconnect=WiFi.getAutoReconnect();WiFi.persistent(false);WiFi.setAutoReconnect(false);
  if(!WiFi.mode(WIFI_STA))return Error::NetworkUnavailable;
  // Like the existing TDoA XIAO network task, avoid modem-sleep latency during
  // short HTTPS sessions; WLAN is powered off again at the end of this job.
  oldSleep=WiFi.getSleep()!=WIFI_PS_NONE;WiFi.setSleep(false);
  const uint32_t started=platform.nowMs();
  WiFi.scanNetworks(true,true);
  int found=WIFI_SCAN_RUNNING;
  while((found=WiFi.scanComplete())==WIFI_SCAN_RUNNING && platform.nowMs()-started<8000){
    if(cancel.cancelled())return Error::Cancelled;platform.waitMs(25);
  }
  struct Candidate {int network,scan,rssi;};Candidate choices[64];unsigned count=0;
  for(int i=0;i<found && count<64;++i)for(unsigned n=0;n<config.count && count<64;++n)
    if(WiFi.SSID(i)==config.networks[n].ssid)choices[count++]={int(n),i,WiFi.RSSI(i)};
  std::sort(choices,choices+count,[](const Candidate&a,const Candidate&b){return a.rssi>b.rssi;});
  if(!count)for(unsigned n=0;n<config.count && count<64;++n)choices[count++]={int(n),-1,0};
  for(unsigned i=0;i<count && platform.nowMs()-started<20000;++i){
    if(cancel.cancelled())return Error::Cancelled;
    auto& choice=choices[i];const auto& network=config.networks[choice.network];
    uint8_t bssid[6];
    if(choice.scan>=0){memcpy(bssid,WiFi.BSSID(choice.scan),6);WiFi.begin(network.ssid,network.password,WiFi.channel(choice.scan),bssid);}
    else WiFi.begin(network.ssid,network.password);
    const uint32_t attempt=platform.nowMs();
    while(platform.nowMs()-attempt<6000 && platform.nowMs()-started<20000){
      if(cancel.cancelled())return Error::Cancelled;
      if(WiFi.isConnected()){
        char message[80];snprintf(message,sizeof(message),"WiFi connected RSSI=%d tx_quarter_dbm=%d",WiFi.RSSI(),int(WiFi.getTxPower()));platform.log(message);
        return Error::None;
      }
      platform.waitMs(25);
    }
    WiFi.disconnect(false,false);
  }
  return Error::NetworkUnavailable;
}
void WifiConnection::sync(WifiConfig& config){
  if(!config.username[0] || !setClock(platform) || cancel.cancelled())return;
  WiFiClientSecure client;HTTPClient http;secure(client,http);
  String url=String(config.management.value())+"/device/config?version="+String(config.version);
  if(!http.begin(client,url))return;
  http.setAuthorization(config.username,config.password);int code=http.GET(),length=http.getSize();
  bool saved=false;
  if(code==200 && length>0 && length<int(WifiConfig::MaxJson)){
    auto* body=new uint8_t[length+1];
    if(body){body[length]=0;if(readExact(http,platform,cancel,body,length))saved=config.merge(reinterpret_cast<char*>(body));memset(body,0,length+1);delete[] body;}
  }
  http.end();client.stop();
  if(!saved){platform.log("WiFi configuration sync unavailable; using validated cache");return;}
  if(cancel.cancelled())return;
  secure(client,http);
  if(http.begin(client,String(config.management.value())+"/device/ack")){
    http.setAuthorization(config.username,config.password);http.addHeader("Content-Type","application/json");
    char payload[48];snprintf(payload,sizeof(payload),"{\"version\":%lu}",static_cast<unsigned long>(config.version));
    if(http.POST(reinterpret_cast<uint8_t*>(payload),strlen(payload))==200)platform.log("Shared WiFi configuration synchronized and acknowledged");
  }
  http.end();client.stop();
}
WifiHttp::WifiHttp(Platform& p,Cancellation& c,const UpdateEndpoint& e,const char* key,const char* name)
  :endpoint(e),platform(p),cancel(c){
  char encoded[100]={};size_t used=0;const char hex[]="0123456789ABCDEF";
  for(const unsigned char* s=(const unsigned char*)name;*s && used+3<sizeof(encoded);++s){
    if((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_')encoded[used++]=*s;
    else{encoded[used++]='%';encoded[used++]=hex[*s>>4];encoded[used++]=hex[*s&15];}
  }
  snprintf(identity,sizeof(identity),"?key=%s&name=%s",key,encoded);
}
bool WifiHttp::prepare(){
  if(cancel.cancelled()){failure=Error::Cancelled;return false;}
  if(!WiFi.isConnected()){failure=Error::NetworkUnavailable;return false;}
  return true;
}
bool WifiHttp::begin(const char* route){
  close();failure=Error::None;readOffset=0;
  if(cancel.cancelled()){failure=Error::Cancelled;return false;}
  char url[360];if(!endpoint.apiUrl(url,sizeof(url),route,identity)){failure=Error::BadConfig;return false;}
  secure(client,http,false);
  if(!http.begin(client,url)){failure=Error::BadConfig;return false;}
  const char* headers[]={"Content-Encoding","Content-Range","Transfer-Encoding"};http.collectHeaders(headers,3);
  http.addHeader("Accept-Encoding","identity");return true;
}
bool WifiHttp::response(int status,int expected){
  if(cancel.cancelled()){failure=Error::Cancelled;return false;}
  if(status<0){
    char ignored[8],message[80];int error=client.lastError(ignored,sizeof(ignored));
    snprintf(message,sizeof(message),"WiFi HTTPS transport failure http=%d tls=%d",status,error);platform.log(message);
    failure=tlsError(client);return false;
  }
  if(status==403){failure=Error::HttpBlocked;return false;}
  if(status!=expected){failure=Error::HttpError;return false;}
  String encoding=http.header("Content-Encoding");
  if((encoding.length() && encoding!="identity") || http.getSize()<0 || http.header("Transfer-Encoding").length()){
    failure=Error::BadResponse;return false;
  }
  actionLength=http.getSize();return true;
}
bool WifiHttp::get(const char* route,int start,int end){
  if(!begin(route))return false;
  if(start>=0){char range[64];snprintf(range,sizeof(range),"bytes=%d-%d",start,end);http.addHeader("Range",range);}
  if(!response(http.GET(),start>=0?206:200))return false;
  if(!actionLength || actionLength>0x330000){failure=Error::BadResponse;return false;}
  if(start>=0){
    unsigned first,last,total;String value=http.header("Content-Range");int consumed=0;
    if(sscanf(value.c_str(),"bytes %u-%u/%u%n",&first,&last,&total,&consumed)!=3 || consumed!=int(value.length()) || first!=unsigned(start) || last!=unsigned(end) || total<=last || total>0x330000 || actionLength!=unsigned(end-start+1)){
      failure=Error::BadResponse;return false;
    }
  }
  return true;
}
bool WifiHttp::readBody(uint32_t offset,uint8_t* data,size_t count){
  if(offset!=readOffset || readOffset>actionLength || count>actionLength-readOffset){failure=Error::BadResponse;return false;}
  if(!readExact(http,platform,cancel,data,count)){failure=cancel.cancelled()?Error::Cancelled:Error::NetworkUnavailable;return false;}
  readOffset+=count;return true;
}
bool WifiHttp::post(const char* payload){
  if(!begin("report"))return false;http.addHeader("Content-Type","application/json");
  return response(http.POST(String(payload)),200);
}
}
#endif
