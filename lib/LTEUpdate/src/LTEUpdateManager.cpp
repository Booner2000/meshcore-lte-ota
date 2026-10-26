#include "LTEUpdateManager.h"
#include "LTECommand.h"
#include <stdio.h>
#include <string.h>

namespace lte {
Error LTEUpdateManager::begin() {
  const Error error = power.begin();
  {
    Lock guard(platform);
    status.ready = error == Error::None;
    status.sleepEnabled = config.sleepControl;
    status.error = error;
  }
  platform.log(error == Error::None ? "Manager initialized; LTE diagnostics and update service" : "Manager configuration rejected");
  platform.log("Power mode: external; EN/PWRKEY untouched");
  platform.log(config.sleepControl ? "Sleep control: enabled; S awake" : "Sleep control: disabled; no S pin access");
  return error;
}
Status LTEUpdateManager::snapshot() { Lock guard(platform); return status; }
bool LTEUpdateManager::cancelled() { Lock guard(platform); return status.cancelRequested; }
bool LTEUpdateManager::handleCommand(const char* command, char* reply, size_t capacity) {
  Operation operation = Operation::None;
  if (commandEquals(command, "start lte update")) {
#if defined(LTE_REAL_OTA) && LTE_REAL_OTA
    operation = Operation::InstallUpdate;
#else
    operation = Operation::SimulateUpdate;
#endif
  }
  else if (commandEquals(command, "start lte download")) operation = Operation::DownloadTest;
  else if (commandEquals(command, "lte update report")) operation = Operation::ReportDownload;
  else if (commandEquals(command, "lte modem test")) operation = Operation::ModemTest;
  else if (commandEquals(command, "lte modem sleep")) operation = Operation::Sleep;
  else if (commandEquals(command, "lte modem wake")) operation = Operation::Wake;
  else if (commandEquals(command, "lte update status")) {
    const Status s = snapshot();
    snprintf(reply, capacity, "%s id=%lu busy=%u %u%% sim=%u done=%u err=%s cleanup=%s",
      stateName(s.state), static_cast<unsigned long>(s.jobId), s.busy, s.progress,
      s.simulated, s.completed, errorName(s.error), errorName(s.cleanupError));
    return true;
  } else if (commandEquals(command, "lte modem status")) {
    const Status s = snapshot();
    // Fixed field budgets, including every requested field, <=150 bytes.
    snprintf(reply, capacity, "AT=%s m=%.16s fw=%.20s SIM=%.10s reg=%.7s CSQ=%.7s S=%s err=%s%s",
      !s.modem.tested ? "?" : s.modem.reachable ? "yes" : "no",
      s.modem.model, s.modem.firmware, s.modem.sim, s.modem.registration,
      s.modem.signal, !s.sleepEnabled ? "off" : s.sleepRequested ? "allowed" : "awake",
      errorName(s.error), s.cleanupError != Error::None ? "/RESTORE" : "");
    return true;
  } else if (commandEquals(command, "lte update cancel")) {
    Lock guard(platform);
    if (status.busy) {
      status.cancelRequested = true;
      snprintf(reply, capacity, "OK cancel requested id=%lu", static_cast<unsigned long>(status.jobId));
    } else snprintf(reply, capacity, "OK idle; nothing to cancel");
    return true;
  } else return false;
  platform.log("CLI operation received");
  // Protect both reservation and queue submission. enqueue MUST be nonblocking.
  Lock guard(platform);
  if (!status.ready) snprintf(reply, capacity, "ERR %s", errorName(status.error));
  else if (status.busy) snprintf(reply, capacity, "BUSY id=%lu", static_cast<unsigned long>(status.jobId));
  else if (operation == Operation::Sleep && !config.sleepControl)
    snprintf(reply, capacity, "ERR SLEEP_DISABLED: configure a safe S GPIO first");
  else {
    const Status previous = status;
    status.busy = true;
    status.cancelRequested = false;
    status.operation = operation;
    status.state = State::PREFLIGHT;
    status.progress = 0;
    status.error = Error::None;
    status.cleanupError = Error::None;
    status.completed = false;
    status.simulated = operation == Operation::SimulateUpdate;
    ++status.jobId;
    if (!platform.enqueue(operation)) {
      status = previous;
      status.error = Error::QueueFull;
      snprintf(reply, capacity, "ERR QUEUE_FULL");
    } else snprintf(reply, capacity, "OK queued id=%lu%s", static_cast<unsigned long>(status.jobId),
                    status.simulated ? " simulation; no flash" : "");
  }
  return true;
}
void LTEUpdateManager::transition(State state, uint8_t progress) {
  {
    Lock guard(platform);
    status.state = state;
    status.progress = progress;
  }
  char message[64];
  snprintf(message, sizeof(message), "State -> %s", stateName(state));
  platform.log(message);
}
bool LTEUpdateManager::delayCancellable(uint32_t ms) {
  const uint32_t start = platform.nowMs();
  for (;;) {
    const uint32_t elapsed = platform.nowMs() - start;
    if (elapsed >= ms) break;
    if (cancelled()) return false;
    const uint32_t remaining = ms - elapsed;
    platform.waitMs(remaining > 20 ? 20 : remaining);
  }
  return !cancelled();
}
void LTEUpdateManager::sleepPermissionChanged(bool allowed) {
  Lock guard(platform);
  status.sleepRequested = allowed;
}
Error LTEUpdateManager::simulate() {
  const State states[] = {State::PREFLIGHT, State::POWER_ON, State::MODEM_BOOT,
    State::SIM_READY, State::REGISTER, State::DATA_READY, State::DOWNLOADING,
    State::VERIFYING, State::READY_TO_COMMIT};
  for (size_t i = 0; i < sizeof(states)/sizeof(states[0]); ++i) {
    if (cancelled()) return Error::Cancelled;
    transition(states[i], static_cast<uint8_t>(i * 100 / 8));
    if (states[i] == State::POWER_ON) {
      Error error = power.powerOn();
      if (error != Error::None) return error;
    }
    if (states[i] == State::MODEM_BOOT && (config.sleepControl || config.simulationUsesModem)) {
      Error error = power.wakeUp(this);
      if (error != Error::None) return error;
    }
    if (!delayCancellable(config.simulationStepMs)) return Error::Cancelled;
  }
  platform.log("Update completed (SIMULATION); no download, flash, activation or reboot");
  return Error::None;
}
Error LTEUpdateManager::diagnose() {
  const char* const commands[] = {"AT", "ATE0", "ATI", "AT+CGMM", "AT+CGMR", "AT+CPIN?", "AT+CEREG?", "AT+CSQ"};
  const char* const prefixes[] = {nullptr, nullptr, nullptr, "", "", "+CPIN:", "+CEREG:", "+CSQ:"};
  ModemStatus modem;
  modem.tested = true;
  {
    Lock guard(platform);
    status.modem = modem;
  }
  transition(State::MODEM_BOOT, 0);
  Error error = power.wakeUp(this);
  if (error != Error::None) return error;
  Error firstError = Error::None;
  for (size_t i = 0; i < 8; ++i) {
    AtResponse response = transport.command(commands[i], i >= 2 && i <= 4 ? config.identityTimeoutMs : config.atTimeoutMs,
                                           this, prefixes[i]);
    modem.commands[i] = response.outcome;
    if (i == 0) modem.reachable = response.outcome == AtOutcome::Ok;
    if (response.extendedError >= 0) modem.extendedError = response.extendedError;
    if (response.outcome == AtOutcome::Ok) {
      switch (i) {
        case 3: safeText(modem.model, sizeof(modem.model), response.value); break;
        case 4: safeText(modem.firmware, sizeof(modem.firmware), response.value); break;
        case 5: safeText(modem.sim, sizeof(modem.sim), response.value); break;
        case 6: safeText(modem.registration, sizeof(modem.registration), response.value); break;
        case 7: safeText(modem.signal, sizeof(modem.signal), response.value); break;
      }
    } else if (firstError == Error::None) firstError = atError(response.outcome);
    {
      Lock guard(platform);
      status.modem = modem;
      status.progress = static_cast<uint8_t>((i + 1) * 100 / 8);
    }
    char message[160];
    // Only whitelisted query names and sanitized values, never the raw response.
    snprintf(message, sizeof(message), "%s -> %s code=%d%s%.65s", commands[i], outcomeName(response.outcome),
      response.extendedError, response.value[0] ? " value=" : "", response.value);
    platform.log(message);
    if (response.outcome == AtOutcome::Cancelled || response.outcome == AtOutcome::Timeout ||
        response.outcome == AtOutcome::Overflow || response.outcome == AtOutcome::UartOverflow ||
        response.outcome == AtOutcome::IoError || (i == 0 && !modem.reachable))
      return atError(response.outcome); // Stop after loss of framing; later commands remain NOT_RUN.
  }
  platform.log(firstError == Error::None ? "Modem test completed" : "Modem test completed with errors");
  return firstError;
}
void LTEUpdateManager::run(Operation operation) {
  // A stale queue entry must never cause a new hardware operation.
  {
    Lock guard(platform);
    if (!status.busy || status.operation != operation) return;
  }
  const bool priorSleep = power.isSleepRequested();
  platform.log(operation == Operation::SimulateUpdate ? "Update job accepted (simulation)" : "Modem job accepted");
  Error error = cancelled() ? Error::Cancelled : Error::None;
  if (error == Error::None) {
    switch (operation) {
      case Operation::SimulateUpdate: error = simulate(); break;
      case Operation::ModemTest: error = diagnose(); break;
      case Operation::Sleep: error = power.setSleep(true, this); break;
      case Operation::Wake: error = power.wakeUp(this); break;
      case Operation::InstallUpdate:
      case Operation::DownloadTest:
      case Operation::ReportDownload:
        transition(State::MODEM_BOOT, 0);
        error = power.wakeUp(this);
        if (error == Error::None) error = operation==Operation::ReportDownload ? platform.reportDownload(*this) : operation==Operation::InstallUpdate ? platform.installUpdate(*this) : platform.downloadUpdate(*this);
        break;
      default: error = Error::BadConfig; break;
    }
  }
  // Observe cancellation before deciding whether a manual sleep/wake needs undo.
  if (error == Error::None && cancelled()) error = Error::Cancelled;
  // Cleanup intentionally ignores user cancel, but each AT request has a deadline.
  Error cleanup = Error::None;
  if ((operation == Operation::ModemTest || operation == Operation::SimulateUpdate || operation == Operation::DownloadTest || operation == Operation::ReportDownload || operation == Operation::InstallUpdate || error != Error::None) &&
      config.sleepControl && power.isSleepRequested() != priorSleep) {
    cleanup = power.setSleep(priorSleep);
    if (cleanup != Error::None) platform.log("Sleep restore failed; S remains awake (fail safe)");
  }
  transport.end();
  if(operation==Operation::InstallUpdate && error==Error::None && cleanup==Error::None){
    // The lock makes cancellation and boot selection mutually exclusive.
    Lock guard(platform);
    error=status.cancelRequested ? Error::Cancelled : platform.activateUpdate(priorSleep);
  }
  const bool finalSleep = power.isSleepRequested();
  {
    Lock guard(platform);
    status.sleepRequested = finalSleep;
    status.error = error;
    status.cleanupError = cleanup;
    status.completed = error == Error::None && cleanup == Error::None;
    status.state = error == Error::Cancelled ? State::CANCELLED :
      error != Error::None || cleanup != Error::None ? State::FAILED :
      operation == Operation::SimulateUpdate ? State::READY_TO_COMMIT : State::IDLE;
    status.busy = false;
  }
  char message[100];
  snprintf(message, sizeof(message), "Operation finished err=%s cleanup=%s", errorName(error), errorName(cleanup));
  platform.log(message);
}
}
