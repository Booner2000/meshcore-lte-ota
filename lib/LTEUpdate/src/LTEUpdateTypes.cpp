#include "LTEUpdateTypes.h"
#include <string.h>

namespace lte {
const char* stateName(State value) {
  static const char* const names[] = {"IDLE", "PREFLIGHT", "POWER_ON", "MODEM_BOOT",
    "SIM_READY", "REGISTER", "DATA_READY", "DOWNLOADING", "VERIFYING",
    "READY_TO_COMMIT", "FAILED", "CANCELLED"};
  const unsigned i = static_cast<unsigned>(value);
  return i < sizeof(names)/sizeof(names[0]) ? names[i] : "UNKNOWN";
}
const char* errorName(Error value) {
  static const char* const names[] = {"NONE", "NOT_READY", "BAD_CONFIG", "BUSY",
    "QUEUE_FULL", "UART_INIT", "UART_IO", "UART_OVERFLOW", "RESPONSE_OVERFLOW",
    "AT_ERROR", "AT_TIMEOUT", "BAD_RESPONSE", "SLEEP_DISABLED", "SLEEP_RESTORE",
    "CANCELLED", "NOT_IMPLEMENTED", "HTTP_BLOCKED", "HTTP_ERROR", "HASH_MISMATCH", "TLS_CONFIG"};
  const unsigned i = static_cast<unsigned>(value);
  return i < sizeof(names)/sizeof(names[0]) ? names[i] : "UNKNOWN";
}
const char* outcomeName(AtOutcome value) {
  static const char* const names[] = {"NOT_RUN", "OK", "ERROR", "TIMEOUT",
    "CANCELLED", "OVERFLOW", "IO_ERROR", "BAD_RESPONSE", "UART_OVERFLOW"};
  const unsigned i = static_cast<unsigned>(value);
  return i < sizeof(names)/sizeof(names[0]) ? names[i] : "UNKNOWN";
}
Error atError(AtOutcome value) {
  switch (value) {
    case AtOutcome::Ok: return Error::None;
    case AtOutcome::Timeout: return Error::AtTimeout;
    case AtOutcome::Cancelled: return Error::Cancelled;
    case AtOutcome::Overflow: return Error::ResponseOverflow;
    case AtOutcome::UartOverflow: return Error::UartOverflow;
    case AtOutcome::IoError: return Error::UartIo;
    case AtOutcome::BadResponse: return Error::BadResponse;
    default: return Error::AtError;
  }
}
void safeText(char* out, size_t capacity, const char* input) {
  if (!capacity) return;
  size_t n = 0;
  while (*input && n + 1 < capacity) {
    if (*input >= '0' && *input <= '9') {
      const char* end = input;
      while (*end >= '0' && *end <= '9') ++end;
      if (end - input >= 8) {
        const char* replacement = "[redacted]";
        while (*replacement && n + 1 < capacity) out[n++] = *replacement++;
        input = end;
        continue;
      }
    }
    const unsigned char c = static_cast<unsigned char>(*input++);
    out[n++] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
  }
  out[n] = 0;
}
}
