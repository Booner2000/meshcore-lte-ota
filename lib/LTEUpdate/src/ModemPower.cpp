#include "ModemPower.h"
#include <stdio.h>
#include <stdlib.h>

namespace lte {
Error ModemPower::begin() {
  if (!config.valid()) return Error::BadConfig;
  // No EN/PWRKEY access. Only an explicitly enabled, validated S pin is touched.
  return setSleep(false);
}
Error ModemPower::powerOn() { return config.powerControl ? Error::NotImplemented : Error::None; }
Error ModemPower::powerOff() { return config.powerControl ? Error::NotImplemented : Error::None; }
Error ModemPower::setSleep(bool enable, Cancellation* cancel) {
  if (transport.active()) return Error::Busy;
  if (!config.sleepControl) return enable ? Error::SleepDisabled : Error::None;
  if (!enable) {
    platform.output(config.sleepPin, config.sAwakeHigh);
    sleepRequested = false;
    if (observer) observer->sleepPermissionChanged(false);
    return Error::None;
  }
  // Always wake and confirm AT readiness first, including repeated sleep requests.
  Error err = wakeUp(cancel);
  if (err != Error::None) return err;
  AtResponse response = transport.command("AT+CSCLK=1", config.atTimeoutMs, cancel);
  char message[72];
  snprintf(message, sizeof(message), "AT+CSCLK=1 -> %s code=%d", outcomeName(response.outcome), response.extendedError);
  platform.log(message);
  if (response.outcome != AtOutcome::Ok) return atError(response.outcome);
  if (cancel && cancel->cancelled()) return Error::Cancelled;
  platform.output(config.sleepPin, !config.sAwakeHigh);
  sleepRequested = true; // Allowed to sleep, NOT confirmation that it is sleeping.
  if (observer) observer->sleepPermissionChanged(true);
  return Error::None;
}
Error ModemPower::setTemporaryBaud(uint32_t rate, Cancellation* cancel) {
  if (rate != 115200 && rate != 230400 && rate != 460800 && rate != 921600) return Error::BadConfig;
  if (cancel && cancel->cancelled()) return Error::Cancelled;
  char command[40];snprintf(command,sizeof(command),"AT+IPR=%lu",static_cast<unsigned long>(rate));
  // IPR is temporary. Never issue IPREX (persistent modem setting).
  AtResponse response=transport.command(command,config.atTimeoutMs,cancel);
  if(response.outcome!=AtOutcome::Ok)return atError(response.outcome);
  // Once accepted, complete host reconfiguration even if cancellation arrives.
  platform.waitMs(500);
  Error error=transport.setBaud(rate);
  if(error!=Error::None)return error;
  platform.waitMs(500);
  response=transport.command("AT",config.atTimeoutMs);
  if(response.outcome!=AtOutcome::Ok)return atError(response.outcome);
  response=transport.command("AT+IPR?",config.atTimeoutMs,nullptr,"+IPR:");
  if(response.outcome!=AtOutcome::Ok)return atError(response.outcome);
  char* end=nullptr;unsigned long reported=strtoul(response.value,&end,10);
  if(end==response.value || *end || reported!=rate)return Error::BadResponse;
  platform.log(rate==config.baud?"UART baseline restored and verified":"UART temporary transfer rate verified");
  return cancel && cancel->cancelled()?Error::Cancelled:Error::None;
}
Error ModemPower::wakeUp(Cancellation* cancel) {
  Error err = setSleep(false);
  if (err != Error::None) return err;
  platform.waitMs(200);
  if (cancel && cancel->cancelled()) return Error::Cancelled;
  err = transport.begin();
  if (err != Error::None) return err;
  const uint32_t first=transport.currentBaud();
  const uint32_t rates[]={first,115200,230400,460800,921600};
  const uint32_t start=platform.nowMs();
  unsigned index=0;
  while(platform.nowMs()-start<config.wakeTimeoutMs){
    if(cancel && cancel->cancelled())return Error::Cancelled;
    uint32_t rate=rates[index++ % 5];
    if(rate==first && index%5==2)continue; // Skip duplicate baseline in normal case.
    err=transport.setBaud(rate);
    if(err!=Error::None)return err;
    platform.waitMs(50);
    uint32_t elapsed=platform.nowMs()-start;
    if(elapsed>=config.wakeTimeoutMs)break;
    uint32_t remaining=config.wakeTimeoutMs-elapsed;
    AtResponse response=transport.command("AT",remaining<1000?remaining:1000,cancel);
    char message[96];snprintf(message,sizeof(message),"Wake AT baud=%lu -> %s",static_cast<unsigned long>(rate),outcomeName(response.outcome));
    platform.log(message);
    if(response.outcome==AtOutcome::Cancelled)return Error::Cancelled;
    if(response.outcome==AtOutcome::Ok){
      if(rate!=config.baud)return setTemporaryBaud(config.baud,cancel);
      return Error::None;
    }
    // Wrong baud may produce framing/overflow faults. Reconfiguration clears
    // these for the next bounded probe; never retry a firmware GET here.
  }
  return Error::AtTimeout;
}
}
