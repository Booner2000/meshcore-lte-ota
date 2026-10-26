#include "A7670Transport.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

namespace lte {
Error A7670Transport::begin() {
  if (started) return Error::None;
  if (!uart.begin(config)) return Error::UartInit;
  started = true;
  char message[64];
  snprintf(message, sizeof(message), "UART%d initialized baud=%lu 8N1", config.uart,
           static_cast<unsigned long>(config.baud));
  platform.log(message);
  return Error::None;
}
void A7670Transport::end() {
  if (started) uart.end();
  started = false;
  transacting = false;
}
AtResponse A7670Transport::command(const char* cmd, uint32_t timeoutMs,
                                 Cancellation* cancel, const char* prefix) {
  AtResponse result;
  if (!started || transacting) { result.outcome = AtOutcome::IoError; return result; }
  if (cancel && cancel->cancelled()) { result.outcome = AtOutcome::Cancelled; return result; }
  transacting = true;
  // Quarantine after a timeout/cancel before sending another untagged AT request.
  if (needsResync) platform.waitMs(150);
  uart.discardInput();
  needsResync = false;
  if (!uart.write(cmd, strlen(cmd)) || !uart.write("\r", 1)) {
    result.outcome = AtOutcome::IoError;
  } else {
    const uint32_t start = platform.nowMs();
    char line[192] = {};
    size_t used = 0;
    size_t total = 0;
    while (platform.nowMs() - start < timeoutMs) {
      if (cancel && cancel->cancelled()) { result.outcome = AtOutcome::Cancelled; break; }
      const Error fault = uart.fault();
      if (fault != Error::None) {
        result.outcome = fault == Error::UartOverflow ? AtOutcome::UartOverflow : AtOutcome::IoError;
        break;
      }
      char c;
      if (!uart.read(c, 10)) continue; // Device read blocks; fake advances its clock.
      ++result.receivedBytes;
      if (++total > 4096) { result.outcome = AtOutcome::Overflow; break; }
      if (c == '\r') continue;
      if (c != '\n') {
        if (used + 1 >= sizeof(line) || c == 0) { result.outcome = AtOutcome::Overflow; break; }
        line[used++] = c;
        continue;
      }
      line[used] = 0;
      if (used) ++result.receivedLines;
      used = 0;
      if (!line[0] || strcmp(line, cmd) == 0) continue;
      if (strcmp(line, "OK") == 0) {
        result.outcome = prefix && !result.value[0] ? AtOutcome::BadResponse : AtOutcome::Ok;
        break;
      }
      if (strcmp(line, "ERROR") == 0 || strncmp(line, "+CME ERROR:", 11) == 0 ||
          strncmp(line, "+CMS ERROR:", 11) == 0) {
        result.outcome = AtOutcome::Error;
        const char* colon = strchr(line, ':');
        if (colon) {
          char* end = nullptr;
          const long code = strtol(colon + 1, &end, 10);
          if (end != colon + 1 && code >= 0 && code <= 65535) result.extendedError = static_cast<int>(code);
        }
        break;
      }
      bool capture = false;
      const char* value = line;
      if (prefix && *prefix) {
        capture = strncmp(line, prefix, strlen(prefix)) == 0;
        value += capture ? strlen(prefix) : 0;
        while (*value == ' ') ++value;
        // +CEREG query has <n>,<stat>; a bare registration URC is not its reply.
        if (capture && strcmp(prefix, "+CEREG:") == 0) {
          char* end = nullptr;
          strtol(value, &end, 10);
          capture = end != value && *end == ',';
          if (capture) { ++end; while (*end == ' ') ++end; capture = *end >= '0' && *end <= '9'; }
        }
      } else if (prefix) {
        capture = line[0] != '+' && line[0] != '^' && strcmp(line, "RDY") != 0 &&
                  strcmp(line, "PB DONE") != 0 && strcmp(line, "SMS DONE") != 0 &&
                  strcmp(line, "RING") != 0;
        // Accept command-specific tagged identity replies as well as plain ones.
        // Unknown +URCs must never become the model or firmware version.
        const char* tag = strcmp(cmd, "AT+CGMM") == 0 ? "+CGMM:" :
                          strcmp(cmd, "AT+CGMR") == 0 ? "+CGMR:" : nullptr;
        if (tag && strncmp(line, tag, strlen(tag)) == 0) {
          capture = true;
          value = line + strlen(tag);
          while (*value == ' ') ++value;
        }
      }
      if (capture && !result.value[0]) safeText(result.value, sizeof(result.value), value);
      else ++result.urcs; // Never log raw URCs/ATI; these can contain identifiers.
    }
    if (result.outcome == AtOutcome::NotRun) result.outcome = AtOutcome::Timeout;
  }
  const Error finalFault = uart.fault();
  if (finalFault != Error::None)
    result.outcome = finalFault == Error::UartOverflow ? AtOutcome::UartOverflow : AtOutcome::IoError;
  needsResync = result.outcome != AtOutcome::Ok && result.outcome != AtOutcome::Error;
  transacting = false;
  return result;
}
}
