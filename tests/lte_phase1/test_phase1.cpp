#include "LTEUpdateManager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

using namespace lte;
static unsigned checks = 0;
#define CHECK(test) do { ++checks; if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

struct FakePlatform : Platform {
  uint32_t clock = 0;
  std::mutex mutex;
  std::deque<Operation> queue;
  std::vector<std::pair<int, bool>> pins;
  std::vector<std::string> logs;
  bool accept = true;
  std::function<void()> onWait;
  std::function<void(const char*)> onLog;
  std::function<void(int,bool)> onPin;
  uint32_t nowMs() override { return clock; }
  void waitMs(uint32_t ms) override { clock += ms; if (onWait) onWait(); }
  void lock() override { mutex.lock(); }
  void unlock() override { mutex.unlock(); }
  bool enqueue(Operation operation) override { if (!accept) return false; queue.push_back(operation); return true; }
  void output(int pin, bool high) override { pins.emplace_back(pin, high); if (onPin) onPin(pin,high); }
  void log(const char* value) override { logs.emplace_back(value); if (onLog) onLog(value); }
  std::string logText() const { std::string out; for (const auto& s : logs) out += s + '\n'; return out; }
};
struct FakeUart : ByteUart {
  FakePlatform& platform;
  bool beginOk = true;
  bool writeOk = true;
  bool begun = false;
  unsigned starts = 0, ends = 0;
  std::string commandBuffer, incoming;
  std::vector<std::string> commands;
  std::map<std::string, std::deque<std::string>> replies;
  Error ioFault = Error::None;
  std::function<void(const std::string&)> onCommand;
  explicit FakeUart(FakePlatform& p) : platform(p) {}
  bool begin(const LTEUpdateConfig&) override { ++starts; begun = beginOk; return beginOk; }
  void end() override { ++ends; begun = false; }
  bool write(const char* data, size_t length) override {
    if (!writeOk) return false;
    for (size_t i = 0; i < length; ++i) {
      if (data[i] != '\r') { commandBuffer += data[i]; continue; }
      commands.push_back(commandBuffer);
      auto& scripted = replies[commandBuffer];
      if (!scripted.empty()) { incoming = scripted.front(); scripted.pop_front(); }
      else if (commandBuffer == "AT+CGMM") incoming = "\r\nA7670E\r\nOK\r\n";
      else if (commandBuffer == "AT+CGMR") incoming = "\r\nV1.2.3\r\nOK\r\n";
      else if (commandBuffer == "AT+CPIN?") incoming = "\r\n+CPIN: READY\r\nOK\r\n";
      else if (commandBuffer == "AT+CEREG?") incoming = "\r\n+CEREG: 5\r\n+CEREG: 0,5\r\nOK\r\n";
      else if (commandBuffer == "AT+CSQ") incoming = "\r\n+CSQ: 20,99\r\nOK\r\n";
      else if (commandBuffer == "ATI") incoming = "\r\nSIMCOM\r\nIMEI: 123456789012345\r\nOK\r\n";
      else incoming = "\r\nOK\r\n";
      if (onCommand) onCommand(commandBuffer);
      commandBuffer.clear();
    }
    return true;
  }
  bool read(char& byte, uint32_t timeout) override {
    if (!incoming.empty()) { byte = incoming.front(); incoming.erase(0,1); return true; }
    platform.waitMs(timeout);
    return false;
  }
  Error fault() override { return ioFault; }
  void discardInput() override { incoming.clear(); ioFault = Error::None; }
};
struct Fixture {
  FakePlatform p;
  FakeUart uart;
  LTEUpdateConfig config;
  LTEUpdateManager manager;
  explicit Fixture(LTEUpdateConfig c = LTEUpdateConfig()) : uart(p), config(c), manager(p, uart, config) {
    CHECK(manager.begin() == Error::None);
  }
  std::string cli(const char* command) {
    char out[CliReplySize];
    CHECK(manager.handleCommand(command, out, sizeof(out)));
    CHECK(strlen(out) <= 150);
    return out;
  }
  void run() {
    CHECK(p.queue.size() == 1);
    const auto operation = p.queue.front(); p.queue.pop_front();
    manager.run(operation);
  }
};
static LTEUpdateConfig sleepConfig() {
  LTEUpdateConfig c; c.sleepControl = true; c.sleepPin = 2; c.simulationStepMs = 30;
  c.atTimeoutMs = 100; c.identityTimeoutMs = 100; c.wakeTimeoutMs = 250;
  return c;
}
static void testCommandsAndSimulation() {
  Fixture f;
  char out[151] = "untouched";
  CHECK(!f.manager.handleCommand("start lte update extra", out, sizeof(out)));
  CHECK(!f.manager.handleCommand("lte modem test extra", out, sizeof(out)));
  CHECK(!strcmp(out, "untouched"));
  CHECK(f.cli("lte modem status").find("AT=?") != std::string::npos);
  CHECK(f.cli("lte modem sleep").find("SLEEP_DISABLED") != std::string::npos);
  const uint32_t before = f.p.clock;
  CHECK(f.cli("start lte update").find("OK queued") == 0);
  CHECK(f.p.clock == before); // Handler performs no UART wait or simulated step.
  CHECK(f.manager.snapshot().busy);
  CHECK(f.cli("start lte update").find("BUSY") == 0);
  CHECK(f.cli("lte modem test").find("BUSY") == 0);
  CHECK(f.cli("lte modem sleep").find("BUSY") == 0);
  CHECK(f.cli("lte modem wake").find("BUSY") == 0);
  CHECK(f.p.queue.size() == 1);
  f.run();
  auto s = f.manager.snapshot();
  CHECK(!s.busy && s.completed && s.simulated && s.progress == 100);
  CHECK(s.state == State::READY_TO_COMMIT && s.error == Error::None);
  CHECK(f.uart.starts == 0 && f.uart.commands.empty() && f.p.pins.empty());
  CHECK(f.p.logText().find("DOWNLOADING") != std::string::npos);
  CHECK(f.p.logText().find("VERIFYING") != std::string::npos);
  CHECK(f.cli("lte update status").find("sim=1") != std::string::npos);
  CHECK(f.cli("lte update cancel").find("idle") != std::string::npos);
  // A completed queue entry cannot be replayed directly.
  const auto count = f.p.logs.size();
  f.manager.run(Operation::SimulateUpdate);
  CHECK(f.p.logs.size() == count);
}
static void testCancelAtEveryStage() {
  const char* stages[] = {"PREFLIGHT", "POWER_ON", "MODEM_BOOT", "SIM_READY",
    "REGISTER", "DATA_READY", "DOWNLOADING", "VERIFYING", "READY_TO_COMMIT"};
  for (const auto stage : stages) {
    Fixture f;
    bool requested = false;
    f.p.onLog = [&](const char* message) {
      if (!requested && std::string(message) == std::string("State -> ") + stage) {
        requested = true; f.cli("lte update cancel");
      }
    };
    f.cli("start lte update"); f.run();
    CHECK(requested);
    CHECK(f.manager.snapshot().state == State::CANCELLED);
    CHECK(!f.manager.snapshot().busy && !f.manager.snapshot().completed);
    CHECK(f.uart.commands.empty());
    f.p.onLog = nullptr;
    CHECK(f.cli("start lte update").find("OK queued") == 0);
    f.cli("lte update cancel"); f.run();
    CHECK(f.manager.snapshot().error == Error::Cancelled);
  }
}
static void testQueueAndConfigFailures() {
  Fixture f;
  f.p.accept = false;
  CHECK(f.cli("start lte update").find("QUEUE_FULL") != std::string::npos);
  CHECK(!f.manager.snapshot().busy && f.manager.snapshot().jobId == 0);
  f.p.accept = true;
  f.cli("start lte update"); f.run();
  CHECK(f.manager.snapshot().jobId == 1);
  for (int unsafePin : {-1,0,3,5,6,7,8,9,19,20,21,38,39,40,41,42,43,44,45,46,48}) {
    auto config = sleepConfig(); config.sleepPin = unsafePin;
    CHECK(!config.valid());
    FakePlatform p; FakeUart uart(p); LTEUpdateManager manager(p, uart, config);
    CHECK(manager.begin() == Error::BadConfig && p.pins.empty() && uart.starts == 0);
  }
  auto config = sleepConfig(); config.powerControl = true;
  CHECK(!config.valid());
  config = sleepConfig(); config.uart = 2; CHECK(!config.valid());
  config = sleepConfig(); config.txPin = 7; CHECK(!config.valid());
}
static void testParser() {
  FakePlatform p; FakeUart uart(p); LTEUpdateConfig config;
  A7670Transport transport(uart, p, config);
  CHECK(transport.begin() == Error::None);
  uart.replies["AT+CSQ"].push_back("\r\nAT+CSQ\r\n+CEREG: 5\r\n+CSQ: 20,99\r\nOK\r\n");
  auto r = transport.command("AT+CSQ", 100, nullptr, "+CSQ:");
  CHECK(r.outcome == AtOutcome::Ok && !strcmp(r.value, "20,99") && r.urcs == 1);
  CHECK(r.receivedBytes > 0 && r.receivedLines >= 2);
  uart.replies["AT+CEREG?"].push_back("\r\n+CEREG: 5\r\nOK\r\n");
  CHECK(transport.command("AT+CEREG?",100,nullptr,"+CEREG:").outcome == AtOutcome::BadResponse);
  uart.replies["AT+CEREG?"].push_back("\r\n+CEREG: 5,\"ABCD\",\"01234567\",7\r\n+CEREG: 0,5\r\nOK\r\n");
  CHECK(!strcmp(transport.command("AT+CEREG?",100,nullptr,"+CEREG:").value,"0,5"));
  uart.replies["AT+CGMM"].push_back("\r\n+UNKNOWN: 123456789012345\r\n+CGMM: A7670E\r\nOK\r\n");
  r = transport.command("AT+CGMM",100,nullptr,"");
  CHECK(r.outcome == AtOutcome::Ok && !strcmp(r.value,"A7670E"));
  uart.replies["AT+CGMR"].push_back("\r\n+CGMR: V1.2.3\r\nOK\r\n");
  CHECK(!strcmp(transport.command("AT+CGMR",100,nullptr,"").value,"V1.2.3"));
  uart.replies["AT"].push_back("\r\n+CME ERROR: 10\r\n");
  r = transport.command("AT",100); CHECK(r.outcome == AtOutcome::Error && r.extendedError == 10);
  uart.replies["AT"].push_back("\r\n+CMS ERROR: 500\r\n");
  CHECK(transport.command("AT",100).extendedError == 500);
  uart.replies["AT"].push_back("\r\nERROR\r\n");
  CHECK(transport.command("AT",100).outcome == AtOutcome::Error);
  uart.replies["AT"].push_back("\r\npartial");
  r = transport.command("AT",100);
  CHECK(r.outcome == AtOutcome::Timeout && r.receivedBytes > 0 && r.receivedLines == 0);
  uart.replies["AT"].push_back(std::string(300,'x'));
  CHECK(transport.command("AT",100).outcome == AtOutcome::Overflow);
  uart.replies["AT"].push_back(std::string("\r\nxx\0yy\r\nOK\r\n",15));
  CHECK(transport.command("AT",100).outcome == AtOutcome::Overflow);
  uart.onCommand = [&](const std::string&) { uart.ioFault = Error::UartOverflow; };
  CHECK(transport.command("AT",100).outcome == AtOutcome::UartOverflow);
  uart.onCommand = nullptr;
  uart.writeOk = false;
  CHECK(transport.command("AT",100).outcome == AtOutcome::IoError);
  uart.writeOk = true;
  transport.end();
  CHECK(transport.command("AT",100).outcome == AtOutcome::IoError);
}
static void testDiagnosticAndRedaction() {
  Fixture f;
  f.uart.replies["AT+CGMR"].push_back("\r\nFW-123456789012345\r\nOK\r\n");
  f.cli("lte modem test"); f.run();
  const auto s = f.manager.snapshot();
  CHECK(s.completed && s.modem.tested && s.modem.reachable);
  CHECK(!strcmp(s.modem.model, "A7670E"));
  CHECK(!strcmp(s.modem.firmware, "FW-[redacted]"));
  CHECK(!strcmp(s.modem.sim,"READY") && !strcmp(s.modem.registration,"0,5"));
  CHECK(!strcmp(s.modem.signal,"20,99"));
  for (auto result : s.modem.commands) CHECK(result == AtOutcome::Ok);
  CHECK(f.uart.starts == 1 && f.uart.ends == 1 && !f.uart.begun);
  CHECK(f.p.pins.empty());
  CHECK(f.p.logText().find("123456789012345") == std::string::npos);
  CHECK(f.p.logText().find("IMEI") == std::string::npos);
  const auto reply = f.cli("lte modem status");
  for (const char* field : {"AT=yes","m=A7670E","fw=", "SIM=READY","reg=0,5","CSQ=20,99","S=off","err=NONE"})
    CHECK(reply.find(field) != std::string::npos);
}
static void testSleepRoundTrip() {
  Fixture f(sleepConfig());
  CHECK(f.p.pins.size() == 1 && f.p.pins.back() == std::make_pair(2,true));
  f.cli("lte modem sleep"); f.run();
  CHECK(f.manager.snapshot().sleepRequested && f.p.pins.back().second == false);
  CHECK(f.uart.commands.back() == "AT+CSCLK=1");
  CHECK(f.cli("lte modem status").find("S=allowed") != std::string::npos);
  f.uart.onCommand = [&](const std::string&) {
    CHECK(f.p.pins.back().second == true);
    CHECK(!f.manager.snapshot().sleepRequested);
  };
  f.cli("lte modem test"); f.run();
  CHECK(f.manager.snapshot().sleepRequested && f.manager.snapshot().completed);
  CHECK(f.p.pins.back().second == false);
  CHECK(f.uart.commands.back() == "AT+CSCLK=1");
  bool sawDownload = false;
  f.p.onLog = [&](const char* line) {
    if (strstr(line,"State -> DOWNLOADING")) {
      CHECK(f.p.pins.back().second == true);
      CHECK(!f.manager.snapshot().sleepRequested);
      sawDownload = true;
    }
  };
  f.cli("start lte update"); f.run();
  CHECK(sawDownload && f.manager.snapshot().sleepRequested);
  f.cli("lte modem wake"); f.run();
  CHECK(!f.manager.snapshot().sleepRequested && f.p.pins.back().second == true);
  // Configurable polarity, but known board default remains HIGH=awake.
  auto inverted = sleepConfig(); inverted.sAwakeHigh = false;
  Fixture g(inverted);
  CHECK(g.p.pins.back().second == false);
  g.cli("lte modem sleep"); g.run(); CHECK(g.p.pins.back().second == true);
}
static void testSleepFailureAndRestore() {
  Fixture f(sleepConfig());
  f.uart.replies["AT+CSCLK=1"].push_back("\r\nERROR\r\n");
  f.cli("lte modem sleep"); f.run();
  CHECK(f.manager.snapshot().error == Error::AtError);
  CHECK(!f.manager.snapshot().sleepRequested && f.p.pins.back().second);
  f.uart.replies["AT+CSCLK=1"].push_back("\r\npartial");
  f.cli("lte modem sleep"); f.run();
  CHECK(f.manager.snapshot().error == Error::AtTimeout && !f.manager.snapshot().sleepRequested);
  CHECK(f.p.pins.back().second);
  f.cli("lte modem sleep"); f.run();
  f.uart.replies["ATI"].push_back("\r\npartial");
  f.cli("lte modem test"); f.run();
  CHECK(f.manager.snapshot().error == Error::AtTimeout);
  CHECK(f.manager.snapshot().cleanupError == Error::None && f.manager.snapshot().sleepRequested);
  f.uart.replies["AT+CSCLK=1"].push_back("\r\nERROR\r\n");
  f.cli("lte modem test"); f.run();
  CHECK(f.manager.snapshot().state == State::FAILED);
  CHECK(f.manager.snapshot().cleanupError == Error::AtError);
  CHECK(!f.manager.snapshot().sleepRequested && f.p.pins.back().second);
  f.cli("lte modem sleep"); f.run();
  f.p.onLog = [&](const char* line) { if (strstr(line,"State -> DOWNLOADING")) f.cli("lte update cancel"); };
  f.cli("start lte update"); f.run();
  CHECK(f.manager.snapshot().state == State::CANCELLED && f.manager.snapshot().sleepRequested);
  f.p.onLog = nullptr;
  f.uart.onCommand = [&](const std::string& cmd) { if (cmd == "ATI") f.cli("lte update cancel"); };
  f.cli("lte modem test"); f.run();
  CHECK(f.manager.snapshot().state == State::CANCELLED && f.manager.snapshot().sleepRequested);
}
static void testExtendedDiagnosticError() {
  Fixture f;
  f.uart.replies["AT+CPIN?"].push_back("\r\n+CME ERROR: 10\r\n");
  f.cli("lte modem test"); f.run();
  const auto s = f.manager.snapshot();
  CHECK(s.state == State::FAILED && s.error == Error::AtError && s.modem.reachable);
  CHECK(s.modem.commands[5] == AtOutcome::Error && s.modem.commands[7] == AtOutcome::Ok);
  CHECK(s.modem.extendedError == 10 && !strcmp(s.modem.sim,"unknown"));
  CHECK(f.p.logText().find("code=10") != std::string::npos);
}
static void testCancelManualSleepBoundary() {
  Fixture f(sleepConfig());
  f.p.onPin = [&](int, bool high) { if (!high) f.cli("lte update cancel"); };
  f.cli("lte modem sleep"); f.run();
  CHECK(f.manager.snapshot().state == State::CANCELLED);
  CHECK(!f.manager.snapshot().sleepRequested && f.p.pins.back().second);
}
static void testFailureClassificationAndBounds() {
  Fixture f(sleepConfig());
  f.uart.beginOk = false;
  f.cli("lte modem test"); f.run();
  CHECK(f.manager.snapshot().error == Error::UartInit && !f.manager.snapshot().modem.reachable);
  f.uart.beginOk = true;
  for (int i=0;i<20;++i) f.uart.replies["AT"].push_back("");
  const auto start = f.p.clock;
  f.cli("lte modem test"); f.run();
  CHECK(f.manager.snapshot().error == Error::AtTimeout);
  CHECK(f.p.clock - start < 1000); // Overall wake deadline, not unbounded AT retries.
  CHECK(!f.uart.begun);
  char tiny[3] = {'x','x','z'};
  CHECK(f.manager.handleCommand("lte modem status", tiny, 2));
  CHECK(tiny[1] == 0 && tiny[2] == 'z');
  // Wrap-safe simulated deadlines.
  Fixture wrap;
  wrap.p.clock = UINT32_MAX - 10;
  wrap.cli("start lte update"); wrap.run();
  CHECK(wrap.manager.snapshot().completed);
}
static void testNoOtaImplementation() {
  FirmwareDownloader downloader;
  OTAInstaller installer;
  FirmwareManifest manifest;
  uint8_t data[32] = {};
  CHECK(downloader.fetchManifest(manifest) == Error::NotImplemented);
  CHECK(downloader.verifySignature(manifest) == Error::NotImplemented);
  CHECK(downloader.checkCompatibility(manifest) == Error::NotImplemented);
  CHECK(downloader.download(manifest) == Error::NotImplemented);
  CHECK(installer.beginInactivePartition(manifest) == Error::NotImplemented);
  CHECK(installer.write(data, sizeof(data)) == Error::NotImplemented);
  CHECK(installer.verifySha256(data) == Error::NotImplemented);
  CHECK(installer.activate() == Error::NotImplemented && installer.rollback() == Error::NotImplemented);
  installer.abort();
}
static void testPowerAndTransactionExclusion() {
  FakePlatform p; FakeUart uart(p); LTEUpdateConfig external;
  external.enablePin = 2; external.pwrkeyPin = 4;
  A7670Transport transport(uart,p,external);
  ModemPower power(p,external,transport);
  CHECK(power.begin() == Error::None);
  CHECK(power.powerOn() == Error::None && power.powerOff() == Error::None);
  CHECK(p.pins.empty());
  CHECK(power.setSleep(true) == Error::SleepDisabled && uart.commands.empty());
  auto config = sleepConfig();
  A7670Transport controlled(uart,p,config);
  ModemPower sleep(p,config,controlled);
  CHECK(sleep.begin() == Error::None && controlled.begin() == Error::None);
  uart.onCommand = [&](const std::string&) {
    const size_t count = p.pins.size();
    CHECK(sleep.setSleep(false) == Error::Busy);
    CHECK(sleep.setSleep(true) == Error::Busy);
    CHECK(sleep.wakeUp() == Error::Busy);
    CHECK(p.pins.size() == count);
  };
  CHECK(controlled.command("AT",100).outcome == AtOutcome::Ok);
  controlled.end();
}
static void testCliWhileWorkerWaits() {
  Fixture f;
  f.uart.replies["ATI"].push_back("\r\npartial");
  std::atomic<bool> entered{false}, release{false};
  f.p.onWait = [&] {
    entered = true;
    while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  };
  f.cli("lte modem test");
  const auto op = f.p.queue.front(); f.p.queue.pop_front();
  std::thread worker([&] { f.manager.run(op); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!entered.load() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(entered.load());
  char out[151];
  const auto start = std::chrono::steady_clock::now();
  CHECK(f.manager.handleCommand("lte update status",out,sizeof(out)));
  CHECK(strstr(out,"busy=1"));
  CHECK(f.manager.handleCommand("lte update cancel",out,sizeof(out)));
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100));
  release = true;
  worker.join();
  CHECK(f.manager.snapshot().state == State::CANCELLED && !f.uart.begun);
}
int main() {
  testCommandsAndSimulation(); testCancelAtEveryStage(); testQueueAndConfigFailures();
  testParser(); testDiagnosticAndRedaction(); testSleepRoundTrip();
  testSleepFailureAndRestore(); testFailureClassificationAndBounds(); testNoOtaImplementation();
  testCliWhileWorkerWaits(); testPowerAndTransactionExclusion(); testExtendedDiagnosticError();
  testCancelManualSleepBoundary();
  printf("PASS: %u checks; 13 test groups; no hardware or flash access\n", checks);
  printf("sizeof(Status)=%zu; sizeof(LTEUpdateManager)=%zu\n", sizeof(Status), sizeof(LTEUpdateManager));
}
