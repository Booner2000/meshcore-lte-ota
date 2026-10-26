#include "LTEUpdateService.h"
#include "LTECommand.h"
#include "A7670Http.h"
#if defined(WITH_LTE_OTA) && WITH_LTE_OTA
#include <Arduino.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string.h>
#include <stdio.h>
#include <atomic>

namespace lte {
namespace {
class EspUart : public ByteUart {
  uart_port_t port = UART_NUM_1;
  QueueHandle_t events = nullptr;
  bool owned = false;
  Error lastFault = Error::None;
  int tx = -1, rx = -1;
public:
#if defined(LTE_HARDWARE_DIAGNOSTICS) && LTE_HARDWARE_DIAGNOSTICS
  std::atomic<uint32_t> txBytes{0}, rxBytes{0}, uartFaults{0};
#endif
  bool begin(const LTEUpdateConfig& config) override {
    if (owned) return true;
    port = static_cast<uart_port_t>(config.uart);
    // Never seize an UART that is already used by another library/bridge.
    if (uart_is_driver_installed(port)) return false;
    uart_config_t settings = {};
    settings.baud_rate = config.baud;
    settings.data_bits = UART_DATA_8_BITS;
    settings.parity = UART_PARITY_DISABLE;
    settings.stop_bits = UART_STOP_BITS_1;
    settings.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    settings.source_clk = UART_SCLK_APB;
    if (uart_driver_install(port, 2048, 0, 16, &events, 0) != ESP_OK) return false;
    owned = true;
    tx = config.txPin; rx = config.rxPin;
    if (uart_param_config(port, &settings) != ESP_OK ||
        uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
      end(); return false;
    }
    lastFault = Error::None;
    return true;
  }
  void end() override {
    if (!owned) return;
    uart_driver_delete(port);
    owned = false;
    events = nullptr;
    // No idle HIGH drive into a subsequently disconnected modem.
    gpio_set_direction(static_cast<gpio_num_t>(tx), GPIO_MODE_INPUT);
    gpio_pullup_dis(static_cast<gpio_num_t>(tx));
    gpio_set_direction(static_cast<gpio_num_t>(rx), GPIO_MODE_INPUT);
    gpio_pullup_dis(static_cast<gpio_num_t>(rx));
  }
  bool write(const char* bytes, size_t count) override {
    if (!owned) return false;
    const int written = uart_write_bytes(port, bytes, count);
#if defined(LTE_HARDWARE_DIAGNOSTICS) && LTE_HARDWARE_DIAGNOSTICS
    if (written > 0) txBytes.fetch_add(written, std::memory_order_relaxed);
#endif
    return written == static_cast<int>(count);
  }
  bool read(char& byte, uint32_t timeoutMs) override {
    if (!owned) return false;
    const int count = uart_read_bytes(port, &byte, 1, pdMS_TO_TICKS(timeoutMs) + 1);
#if defined(LTE_HARDWARE_DIAGNOSTICS) && LTE_HARDWARE_DIAGNOSTICS
    if (count > 0) rxBytes.fetch_add(count, std::memory_order_relaxed);
#endif
    if (count < 0) lastFault = Error::UartIo;
    return count == 1;
  }
  Error fault() override {
    uart_event_t event;
    if (events) {
      for (unsigned i = 0; i < 16 && xQueueReceive(events, &event, 0) == pdTRUE; ++i) {
        if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
          lastFault = Error::UartOverflow;
#if defined(LTE_HARDWARE_DIAGNOSTICS) && LTE_HARDWARE_DIAGNOSTICS
          uartFaults.fetch_add(1, std::memory_order_relaxed);
#endif
        } else if (event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR || event.type == UART_BREAK) {
          lastFault = Error::UartIo;
#if defined(LTE_HARDWARE_DIAGNOSTICS) && LTE_HARDWARE_DIAGNOSTICS
          uartFaults.fetch_add(1, std::memory_order_relaxed);
#endif
        }
      }
    }
    return lastFault;
  }
  void discardInput() override {
    if (!owned) return;
    uart_flush_input(port);
    xQueueReset(events);
    lastFault = Error::None;
  }
};
class EspPlatform : public Platform {
public:
  ByteUart* uart = nullptr;
  LTEUpdateManager* manager = nullptr;
  char publicKey[65] = {};
  uint8_t binaryKey[32] = {};
  char stationName[33] = {};
  uint32_t utc = 0;
  uint32_t currentUtc() override { Lock guard(*this); return utc; }
  SemaphoreHandle_t mutex = nullptr;
  QueueHandle_t jobs = nullptr;
  QueueHandle_t logs = nullptr;
  TaskHandle_t worker = nullptr;
  uint32_t nowMs() override { return millis(); }
  void waitMs(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms) + 1); }
  void lock() override { xSemaphoreTake(mutex, portMAX_DELAY); }
  void unlock() override { xSemaphoreGive(mutex); }
  bool enqueue(Operation op) override { return xQueueSend(jobs, &op, 0) == pdTRUE; }
  void output(int pin, bool high) override {
    // Set the output latch BEFORE enabling output, avoiding a LOW wake glitch.
    gpio_set_level(static_cast<gpio_num_t>(pin), high ? 1 : 0);
    // Input-enable permits non-invasive digital readback of the S pad.
    gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT_OUTPUT);
  }
  void log(const char* message) override {
    char line[176] = {};
    const int length = snprintf(line, sizeof(line), "[LTE] %s\n", message);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(line)) return;
    // Only MainBoard::loop prints: worker USB writes could interleave with or
    // drop the original CLI's multi-write response on ESP32 HWCDC.
    if (logs) xQueueSend(logs, line, 0);
  }
  Error httpJob(Cancellation& cancel,bool reportOnly) {
    char key[65], name[33];
    { Lock guard(*this); memcpy(key,publicKey,sizeof(key)); memcpy(name,stationName,sizeof(name)); }
    if (!key[0] || !name[0] || !uart || !manager) return Error::BadConfig;
    A7670Http http(*uart,*this,*manager,cancel,key,name);
    return http.download(reportOnly);
  }
  Error downloadUpdate(Cancellation& cancel) override { return httpJob(cancel,false); }
  Error reportDownload(Cancellation& cancel) override { return httpJob(cancel,true); }
  void print(const char* line) {
    const size_t length = strlen(line);
    if (Serial.availableForWrite() >= static_cast<int>(length))
      Serial.write(reinterpret_cast<const uint8_t*>(line), length);
  }
  void flushLogs() {
    char line[176];
    for (unsigned i = 0; i < 8 && xQueueReceive(logs, line, 0) == pdTRUE; ++i) print(line);
  }
};
struct Service {
  LTEUpdateConfig config;
  EspPlatform platform;
  EspUart uart;
  LTEUpdateManager manager;
  bool running = false;
  bool attempted = false;
  Service() : manager(platform, uart, config) { platform.uart=&uart; platform.manager=&manager; config.taskStackBytes=16384; }
  static void task(void* context) {
    Service& self = *static_cast<Service*>(context);
    Operation op;
    for (;;) {
      if (xQueueReceive(self.platform.jobs, &op, portMAX_DELAY) == pdTRUE) {
        self.manager.run(op);
      }
    }
  }
  bool begin() {
    if (attempted) return running;
    attempted = true;
    platform.mutex = xSemaphoreCreateMutex();
    platform.jobs = xQueueCreate(1, sizeof(Operation));
    platform.logs = xQueueCreate(8, 176);
    if (!platform.mutex || !platform.jobs || !platform.logs) return false;
    if (manager.begin() != Error::None) return false;
    running = xTaskCreatePinnedToCore(task, "LTEUpdate", config.taskStackBytes,
                                    this, 1, &platform.worker, 0) == pdPASS;
    if (!running) platform.log("Worker creation failed");
    return running;
  }
};
Service& service() { static Service instance; return instance; }
}
bool beginService() { return service().begin(); }
bool serviceBusy() { return service().running && service().manager.snapshot().busy; }
void pollService() { if (service().platform.logs) service().platform.flushLogs(); }
void setStationIdentity(const uint8_t* key, const char* name, uint32_t utc) {
  Service& self=service();
  if(!self.running || !key || !name) return;
  Lock guard(self.platform);
  self.platform.utc=utc;
  if(!self.platform.publicKey[0] || memcmp(self.platform.binaryKey,key,32)) {
    memcpy(self.platform.binaryKey,key,32);
    for(unsigned i=0;i<32;++i) snprintf(self.platform.publicKey+i*2,3,"%02x",key[i]);
  }
  if(strncmp(self.platform.stationName,name,sizeof(self.platform.stationName)-1))
    snprintf(self.platform.stationName,sizeof(self.platform.stationName),"%s",name);
}
bool serviceCommand(const char* command, char* reply, size_t capacity) {
  Service& self = service();
#if defined(LTE_HARDWARE_DIAGNOSTICS) && LTE_HARDWARE_DIAGNOSTICS
  if (commandEquals(command, "lte modem io") && self.running) {
    const int level = self.config.sleepControl ? gpio_get_level(static_cast<gpio_num_t>(self.config.sleepPin)) : -1;
    snprintf(reply, capacity, "uart=%d tx=%d rx=%d baud=%lu S=%d level=%d txbytes=%lu rxbytes=%lu faults=%lu",
      self.config.uart, self.config.txPin, self.config.rxPin, static_cast<unsigned long>(self.config.baud),
      self.config.sleepPin, level, static_cast<unsigned long>(self.uart.txBytes.load()),
      static_cast<unsigned long>(self.uart.rxBytes.load()), static_cast<unsigned long>(self.uart.uartFaults.load()));
    return true;
  }
#endif
  if (!self.running) {
    if (strncmp(command, "lte ", 4) != 0 && !commandEquals(command, "start lte update")) return false;
    snprintf(reply, capacity, "ERR LTE_NOT_READY: task/configuration");
    return true;
  }
  return self.manager.handleCommand(command, reply, capacity);
}
}
#endif
