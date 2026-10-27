#pragma once
#include <stdint.h>
#include <stddef.h>

#ifndef LTE_MODEM_POWER_CONTROL
#define LTE_MODEM_POWER_CONTROL 0
#endif
#ifndef LTE_MODEM_SLEEP_CONTROL
#define LTE_MODEM_SLEEP_CONTROL 0
#endif
#ifndef LTE_MODEM_SLEEP_PIN
#define LTE_MODEM_SLEEP_PIN -1
#endif
#ifndef LTE_MODEM_S_AWAKE_HIGH
#define LTE_MODEM_S_AWAKE_HIGH 1
#endif
#ifndef LTE_MODEM_UART_BAUD
#define LTE_MODEM_UART_BAUD 115200
#endif
#ifndef LTE_MODEM_TRANSFER_BAUD
#define LTE_MODEM_TRANSFER_BAUD 460800
#endif

namespace lte {
struct LTEUpdateConfig {
  int uart = 1;
  int txPin = 43;
  int rxPin = 44;
  uint32_t baud = LTE_MODEM_UART_BAUD;
  uint32_t transferBaud = LTE_MODEM_TRANSFER_BAUD;
  bool powerControl = LTE_MODEM_POWER_CONTROL != 0;
  int enablePin = -1;
  int pwrkeyPin = -1;
  bool sleepControl = LTE_MODEM_SLEEP_CONTROL != 0;
  int sleepPin = LTE_MODEM_SLEEP_PIN;
  // External S input, NOT the inverted signal inside the modem board.
  bool sAwakeHigh = LTE_MODEM_S_AWAKE_HIGH != 0;
  uint32_t atTimeoutMs = 2000;
  uint32_t identityTimeoutMs = 5000;
  uint32_t wakeTimeoutMs = 15000;
  uint32_t simulationStepMs = 350;
  bool simulationUsesModem = false;
  uint32_t taskStackBytes = 6144;
  // This validator deliberately only supports the audited B2B WIO target.
  bool valid() const {
    if (uart != 1 || txPin != 43 || rxPin != 44 || baud != 115200 ||
        (transferBaud != 115200 && transferBaud != 230400 && transferBaud != 460800) ||
        atTimeoutMs < 20 || atTimeoutMs > 30000 || identityTimeoutMs < 20 ||
        identityTimeoutMs > 30000 || wakeTimeoutMs < 20 || wakeTimeoutMs > 60000 ||
        simulationStepMs == 0 || simulationStepMs > 10000 ||
        taskStackBytes < 4096 || taskStackBytes > 16384) return false;
    // Phase 1 has no power-switch implementation. Fail closed if requested.
    if (powerControl) return false;
    // No strapping, radio, USB, I2C, flash/PSRAM or UART pins for S.
    return !sleepControl || sleepPin == 1 || sleepPin == 2 || sleepPin == 4;
  }
};
constexpr size_t CliReplySize = 151; // <=150 bytes in existing 160-byte buffers
}
