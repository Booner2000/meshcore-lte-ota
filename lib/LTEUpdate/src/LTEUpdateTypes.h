#pragma once
#include <stdint.h>
#include <stddef.h>

namespace lte {
enum class State : uint8_t {
  IDLE, PREFLIGHT, POWER_ON, MODEM_BOOT, SIM_READY, REGISTER, DATA_READY,
  DOWNLOADING, VERIFYING, READY_TO_COMMIT, FAILED, CANCELLED
};
enum class Operation : uint8_t { None, SimulateUpdate, ModemTest, Sleep, Wake, DownloadTest, ReportDownload, InstallUpdate };
enum class UpdateRoute : uint8_t { Auto, Wifi, Lte };
enum class Error : uint8_t {
  None, NotReady, BadConfig, Busy, QueueFull, UartInit, UartIo,
  UartOverflow, ResponseOverflow, AtError, AtTimeout, BadResponse,
  SleepDisabled, SleepRestore, Cancelled, NotImplemented, HttpBlocked, HttpError, HashMismatch, TlsConfig, SignatureInvalid, FlashError, DataBudget, NetworkUnavailable
};
enum class AtOutcome : uint8_t {
  NotRun, Ok, Error, Timeout, Cancelled, Overflow, IoError, BadResponse, UartOverflow
};
struct AtResponse {
  AtOutcome outcome = AtOutcome::NotRun;
  int extendedError = -1;
  char value[96] = {};
  unsigned urcs = 0;
  uint32_t receivedBytes = 0;
  unsigned receivedLines = 0;
};
struct ModemStatus {
  bool tested = false;
  bool reachable = false;
  char model[25] = "unknown";
  char firmware[33] = "unknown";
  char sim[17] = "unknown";
  char registration[12] = "unknown";
  char signal[12] = "unknown";
  AtOutcome commands[8] = {};
  int extendedError = -1;
};
struct Status {
  bool ready = false;
  bool busy = false;
  bool cancelRequested = false;
  bool sleepEnabled = false;
  bool sleepRequested = false; // Permission to sleep, never a measured sleep state
  bool simulated = false;
  bool completed = false;
  uint32_t jobId = 0;
  uint8_t progress = 0;
  State state = State::IDLE;
  Operation operation = Operation::None;
  UpdateRoute route = UpdateRoute::Lte;
  UpdateRoute activeRoute = UpdateRoute::Auto;
  Error error = Error::None;
  Error cleanupError = Error::None;
  ModemStatus modem;
};
const char* stateName(State state);
const char* errorName(Error error);
const char* outcomeName(AtOutcome outcome);
Error atError(AtOutcome outcome);
// Removes control characters and long digit strings, even in model/version data.
void safeText(char* out, size_t capacity, const char* input);
}
