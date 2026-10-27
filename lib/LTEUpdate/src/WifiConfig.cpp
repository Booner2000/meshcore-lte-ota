#include "WifiConfig.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include <Preferences.h>
#include <cJSON.h>
#include <string.h>
#include <atomic>
namespace lte {
namespace {
char provisionBuffer[WifiConfig::MaxJson+16]={};
size_t provisionUsed=0;
std::atomic<bool> provisionActive{false};
bool provisionRejected=false,provisionDiscard=false;
uint32_t provisionSince=0;
const char* text(cJSON* root,const char* key){cJSON* v=cJSON_GetObjectItemCaseSensitive(root,key);return cJSON_IsString(v)?v->valuestring:nullptr;}
bool printable(const char* value,size_t lo,size_t hi,bool ascii=false){
  if(!value || strlen(value)<lo || strlen(value)>hi)return false;
  for(const unsigned char* s=(const unsigned char*)value;*s;++s)if(*s<32 || *s==127 || (ascii && *s>126))return false;
  return true;
}
}
bool WifiConfig::decode(const char* input){
  if(!input || strlen(input)>=MaxJson)return false;
  cJSON* root=cJSON_Parse(input);if(!root)return false;
  cJSON* schema=cJSON_GetObjectItemCaseSensitive(root,"schema"),*v=cJSON_GetObjectItemCaseSensitive(root,"version"),*rows=cJSON_GetObjectItemCaseSensitive(root,"networks");
  bool ok=cJSON_IsObject(root) && cJSON_IsNumber(schema) && schema->valuedouble==1 && cJSON_IsNumber(v) && v->valuedouble>=1 && v->valuedouble<=2147483647 && v->valuedouble==v->valueint && cJSON_IsArray(rows) && cJSON_GetArraySize(rows)<=16;
  count=0;
  if(ok){cJSON* row;cJSON_ArrayForEach(row,rows){
    const char* ssid=text(row,"ssid"),*pw=text(row,"password"),*security=text(row,"security");
    bool open=security && !strcmp(security,"open"),wpa=security && !strcmp(security,"wpa");
    if(!printable(ssid,1,32) || !pw || (!open&&!wpa) || (open && *pw) || (wpa && !printable(pw,8,63,true))){ok=false;break;}
    for(unsigned i=0;i<count;++i)if(!strcmp(networks[i].ssid,ssid))ok=false;
    if(!ok)break;
    strlcpy(networks[count].ssid,ssid,sizeof(networks[count].ssid));strlcpy(networks[count].password,pw,sizeof(networks[count].password));++count;
  }}
  const char* url=text(root,"management_url"),*user=text(root,"username"),*pw=text(root,"password");
  if(!url || !management.assign(url) || !printable(user,1,64,true) || strchr(user,':') || !printable(pw,12,128,true))ok=false;
  if(ok){version=v->valueint;strlcpy(username,user,sizeof(username));strlcpy(password,pw,sizeof(password));}
  cJSON_Delete(root);if(!ok){count=0;version=0;}return ok;
}
bool WifiConfig::load(){
  Preferences p;if(!p.begin("wifiota",true))return false;String value=p.getString("config","");p.end();return decode(value.c_str());
}
bool WifiConfig::save(const char* input){
  WifiConfig checked;if(!checked.decode(input))return false;
  Preferences p;if(!p.begin("wifiota",false))return false;
  bool ok=p.getString("config","")==input || p.putString("config",input)==strlen(input);
  ok=ok && p.getString("config","")==input;p.end();if(ok)*this=checked;return ok;
}
bool WifiConfig::merge(const char* input){
  cJSON* root=cJSON_Parse(input);if(!root)return false;
  cJSON* unchanged=cJSON_GetObjectItemCaseSensitive(root,"unchanged");
  if(cJSON_IsTrue(unchanged)){
    cJSON* v=cJSON_GetObjectItemCaseSensitive(root,"version");bool ok=cJSON_IsNumber(v) && v->valuedouble==version;cJSON_Delete(root);return ok;
  }
  cJSON_DeleteItemFromObjectCaseSensitive(root,"management_url");cJSON_DeleteItemFromObjectCaseSensitive(root,"username");cJSON_DeleteItemFromObjectCaseSensitive(root,"password");
  cJSON_AddStringToObject(root,"management_url",management.value());cJSON_AddStringToObject(root,"username",username);cJSON_AddStringToObject(root,"password",password);
  char* json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
  WifiConfig checked;bool ok=json && checked.decode(json) && checked.version>=version && save(json);if(json){memset(json,0,strlen(json));cJSON_free(json);}return ok;
}
bool pollWifiProvisioning(bool busy){
  if(!provisionActive){
    if(!Serial.available() || Serial.peek()!='!')return false;
    Serial.read();startWifiProvisioning(busy);
  }
  for(unsigned i=0;i<128 && Serial.available();++i){
    char c=Serial.read();
    if(c=='\r' || c=='\n'){
      provisionBuffer[provisionUsed]=0;bool ok=false;
      if(!provisionRejected && !provisionDiscard && !strncmp(provisionBuffer,"!wifi ",6)){WifiConfig config;ok=config.save(provisionBuffer+6);}
      Serial.println(ok?"WIFI_PROVISION_OK":"WIFI_PROVISION_ERROR");
      memset(provisionBuffer,0,sizeof(provisionBuffer));provisionUsed=0;provisionActive=false;return true;
    }
    if(!provisionDiscard){if(provisionUsed+1<sizeof(provisionBuffer))provisionBuffer[provisionUsed++]=c;else provisionRejected=true;}
  }
  if(!provisionDiscard && millis()-provisionSince>15000){
    memset(provisionBuffer,0,sizeof(provisionBuffer));provisionUsed=0;provisionDiscard=true;
    // Consume through the delimiter even after timeout; late secret bytes must
    // never fall through to the ordinary CLI's echo path.
    Serial.println("WIFI_PROVISION_TIMEOUT");
  }
  return true;
}
void startWifiProvisioning(bool busy){
  memset(provisionBuffer,0,sizeof(provisionBuffer));provisionBuffer[0]='!';provisionUsed=1;
  provisionRejected=busy;provisionDiscard=false;provisionSince=millis();provisionActive=true;
}
bool wifiProvisioningActive(){return provisionActive.load();}
}
#endif
