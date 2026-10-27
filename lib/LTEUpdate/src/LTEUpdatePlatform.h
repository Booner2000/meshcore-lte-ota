#pragma once
#include "LTEUpdateConfig.h"
#include "LTEUpdateTypes.h"

namespace lte {
class Cancellation;
// Implemented by FreeRTOS on the device and a deterministic fake in host tests.
class Platform {
public:
  virtual ~Platform() = default;
  virtual uint32_t nowMs() = 0;
  virtual void waitMs(uint32_t ms) = 0;
  virtual void lock() = 0;
  virtual void unlock() = 0;
  virtual bool enqueue(Operation operation) = 0;
  virtual void output(int pin, bool high) = 0;
  virtual void log(const char* message) = 0;
  virtual Error downloadUpdate(Cancellation&) { return Error::NotImplemented; }
  virtual Error installUpdate(Cancellation&) { return Error::NotImplemented; }
  virtual Error activateUpdate(bool) { return Error::NotImplemented; }
  virtual Error reportDownload(Cancellation&) { return Error::NotImplemented; }
  virtual uint32_t currentUtc() { return 0; }
  // Device implementation checks every rollback-capable image, off the radio task.
  virtual bool fastUartSafe() { return false; }
  virtual Error preferredUpdate(Operation, Cancellation&) { return Error::NotImplemented; }
};
class Lock {
  Platform& platform;
public:
  explicit Lock(Platform& p) : platform(p) { platform.lock(); }
  ~Lock() { platform.unlock(); }
};
class ByteUart {
public:
  virtual ~ByteUart() = default;
  virtual bool begin(const LTEUpdateConfig& config) = 0;
  virtual void end() = 0;
  virtual bool write(const char* bytes, size_t count) = 0;
  virtual bool read(char& byte, uint32_t timeoutMs) = 0;
  virtual Error fault() = 0;
  virtual void discardInput() = 0;
};
class Cancellation {
public:
  virtual ~Cancellation() = default;
  virtual bool cancelled() = 0;
};
}
