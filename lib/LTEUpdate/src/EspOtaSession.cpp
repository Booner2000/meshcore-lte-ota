#include "EspOtaSession.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <string.h>
#include <atomic>
#ifndef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error LTE OTA requires a rollback-enabled SDK and matching installed bootloader
#endif
// Arduino must not confirm an image before MeshCore has initialized its radio.
extern "C" bool verifyRollbackLater() { return true; }
namespace lte {
const char OtaCapability[] = "MESHCORE_LTE_OTA:xiao-s3-wio-b2b-lte-v1:2";
namespace {
std::atomic<bool> pending{false};
esp_timer_handle_t bootTimer = nullptr;
uint32_t healthySince = 0, loops = 0;
bool suppressHealth = false;
void bootExpired(void*) { esp_restart(); } // Pending image rolls back on reset.
bool matchingPending(Preferences& p) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  return running && p.getUInt("part",0) == running->address;
}
}
uint32_t otaReleaseFloor() {
  Preferences p;
  if(!p.begin("lteota",true)) return 0;
  uint32_t floor=p.getUInt("floor",0);p.end();return floor;
}
EspOtaSession::~EspOtaSession() { if(handle) esp_ota_abort(handle); }
Error EspOtaSession::begin(uint32_t size,uint32_t release,const char* firmware,const char* sha) {
  if(handle || finished || !size || !release || release<=otaReleaseFloor()) return Error::BadConfig;
  const esp_partition_t* running=esp_ota_get_running_partition();
  partition=esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t state;
  if(!running || !partition || partition->address==running->address || size>partition->size ||
    (esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY)) return Error::NotReady;
  length=size;counter=release;
  snprintf(id,sizeof(id),"%s",firmware);snprintf(hash,sizeof(hash),"%s",sha);
  return esp_ota_begin(partition,size,&handle)==ESP_OK ? Error::None : Error::FlashError;
}
Error EspOtaSession::write(uint32_t offset,const uint8_t* data,size_t count) {
  if(!handle || offset>length || count>length-offset) return Error::BadConfig;
  return esp_ota_write_with_offset(handle,data,count,offset)==ESP_OK ? Error::None : Error::FlashError;
}
Error EspOtaSession::finish() {
  if(!handle) return Error::NotReady;
  const esp_err_t result=esp_ota_end(handle);handle=0;
  if(result!=ESP_OK) return Error::FlashError;
  // Independent readback proves that the inactive flash matches the signed hash.
  uint8_t digest[32];
  // esp_partition_get_sha256 hashes the ESP image excluding appended hash/padding;
  // use an explicit raw-size hash instead, implemented below.
  extern bool otaRawHash(const esp_partition_t*,uint32_t,uint8_t*);
  if(!otaRawHash(partition,length,digest)) return Error::FlashError;
  char actual[65];for(unsigned i=0;i<32;++i)snprintf(actual+i*2,3,"%02x",digest[i]);
  if(strcmp(actual,hash))return Error::HashMismatch;
  finished=true;return Error::None;
}
Error EspOtaSession::activate(bool sleepAllowed) {
  if(!finished || !partition) return Error::NotReady;
  Preferences p;
  if(!p.begin("lteota",false))return Error::FlashError;
  bool ok=p.putBool("bootSleep",sleepAllowed)==1 && p.putString("id",id)>0 && p.putString("hash",hash)>0 &&
    p.putUInt("counter",counter)==4 && p.putUInt("part",partition->address)==4 && p.putBool("reported",false)==1;
  p.end();
  if(!ok)return Error::FlashError;
  return esp_ota_set_boot_partition(partition)==ESP_OK ? Error::None : Error::FlashError;
}
void otaBootBegin() {
  const esp_partition_t* running=esp_ota_get_running_partition();esp_ota_img_states_t state;
  pending=running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY;
  if(pending){
    Preferences probe;
    if(probe.begin("lteota",false)){
      suppressHealth=probe.getUInt("probePart",0)==running->address;
      if(suppressHealth)probe.remove("probePart");
      probe.end();
    }
    esp_timer_create_args_t args={};args.callback=bootExpired;args.name="ota-health";
    if(esp_timer_create(&args,&bootTimer)!=ESP_OK || esp_timer_start_once(bootTimer,60000000)!=ESP_OK) esp_restart();
  }
}
bool otaRestoreSleep(){Preferences p;if(!p.begin("lteota",true))return false;bool value=p.getBool("bootSleep",false);p.end();return value;}
bool otaPendingHealth(){return pending.load();}
void otaHealthTick() {
  if(!pending || suppressHealth)return;
  if(!healthySince)healthySince=millis();
  if(++loops<100 || millis()-healthySince<15000)return;
  Preferences p;if(!p.begin("lteota",false))return;
  if(!matchingPending(p)){p.end();return;}
  // Persist floor first: a power loss may skip a version but must never permit downgrade.
  uint32_t release=p.getUInt("counter",0);
  if(!release || p.putUInt("floor",release)!=4){p.end();return;}
  p.end();
  if(esp_ota_mark_app_valid_cancel_rollback()!=ESP_OK)return;
  pending=false;if(bootTimer)esp_timer_stop(bootTimer);
  Serial.println("[LTE] OTA HEALTH CONFIRMED: radio setup and main loop alive; rollback cancelled");
}
void otaStatus(char* reply,size_t size) {
  const esp_partition_t* running=esp_ota_get_running_partition();esp_ota_img_states_t state=ESP_OTA_IMG_UNDEFINED;
  if(running)esp_ota_get_state_partition(running,&state);
  snprintf(reply,size,"OTA slot=%s state=%d pending=%u floor=%lu capability=%s",running?running->label:"?",int(state),pending.load(),static_cast<unsigned long>(otaReleaseFloor()),OtaCapability);
}
bool otaInstalledReport(char* firmware,char* sha) {
  if(pending)return false;
  Preferences p;if(!p.begin("lteota",true))return false;
  bool ok=matchingPending(p) && !p.getBool("reported",false) && p.getUInt("counter",0)==p.getUInt("floor",0);
  if(ok){p.getString("id",firmware,25);p.getString("hash",sha,65);}
  p.end();return ok;
}
Error otaRollbackProbe(){
  const esp_partition_t* running=esp_ota_get_running_partition();
  const esp_partition_t* other=esp_ota_get_next_update_partition(nullptr);
  esp_ota_img_states_t state;esp_app_desc_t desc;
  if(pending || !running || !other || esp_ota_get_state_partition(running,&state)!=ESP_OK || state!=ESP_OTA_IMG_VALID || esp_ota_get_partition_description(other,&desc)!=ESP_OK)return Error::NotReady;
  Preferences p;if(!p.begin("lteota",false))return Error::FlashError;
  bool ok=p.putUInt("probePart",other->address)==4;p.end();
  if(!ok || esp_ota_set_boot_partition(other)!=ESP_OK)return Error::FlashError;
  esp_timer_create_args_t args={};args.callback=bootExpired;args.name="ota-probe";
  esp_timer_handle_t timer=nullptr;
  if(esp_timer_create(&args,&timer)!=ESP_OK || esp_timer_start_once(timer,1500000)!=ESP_OK){esp_ota_set_boot_partition(running);return Error::NotReady;}
  return Error::None;
}
void otaReportAcknowledged(){Preferences p;if(p.begin("lteota",false)){p.putBool("reported",true);p.end();}}
}
#include <mbedtls/sha256.h>
namespace lte {
bool otaRawHash(const esp_partition_t* partition,uint32_t size,uint8_t* result){
  mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);mbedtls_sha256_starts_ret(&sha,0);
  uint8_t buffer[1024];bool ok=true;
  for(uint32_t offset=0;offset<size;offset+=sizeof(buffer)){
    size_t count=std::min(size_t(size-offset),sizeof(buffer));
    if(esp_partition_read(partition,offset,buffer,count)!=ESP_OK){ok=false;break;}
    mbedtls_sha256_update_ret(&sha,buffer,count);delay(1);
  }
  mbedtls_sha256_finish_ret(&sha,result);mbedtls_sha256_free(&sha);return ok;
}
}
#endif
