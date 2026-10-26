#include "ModemPower.h"
#include <stdio.h>

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
Error ModemPower::wakeUp(Cancellation* cancel) {
  Error err = setSleep(false);
  if (err != Error::None) return err;
  // Allow modem UART levels to settle after releasing sleep before attaching
  // the RX peripheral/sending the first byte; observed break/frame errors on
  // immediate access after an extended sleep permission on the actual board.
  platform.waitMs(200);
  if (cancel && cancel->cancelled()) return Error::Cancelled;
  err = transport.begin();
  if (err != Error::None) return err;
  const uint32_t start = platform.nowMs();
  for (;;) {
    const uint32_t elapsed = platform.nowMs() - start;
    if (elapsed >= config.wakeTimeoutMs) break;
    if (cancel && cancel->cancelled()) return Error::Cancelled;
    const uint32_t remaining = config.wakeTimeoutMs - elapsed;
    const uint32_t attempt = remaining < 1000 ? remaining : 1000;
    AtResponse response = transport.command("AT", attempt, cancel);
    char message[72];
    snprintf(message, sizeof(message), "Wake AT -> %s code=%d rx=%lu lines=%u", outcomeName(response.outcome),
             response.extendedError, static_cast<unsigned long>(response.receivedBytes), response.receivedLines);
    platform.log(message);
    if (response.outcome == AtOutcome::Ok) return Error::None;
    if (response.outcome != AtOutcome::Timeout && response.outcome != AtOutcome::Error)
      return atError(response.outcome);
    platform.waitMs(50);
  }
  return Error::AtTimeout;
}
}
