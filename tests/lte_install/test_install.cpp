#define main phaseOneTests
#include "../lte_phase1/test_phase1.cpp"
#undef main
struct InstallPlatform : FakePlatform {
  Error result=Error::None, activation=Error::None;
  unsigned downloads=0, activations=0;
  std::function<void()> duringDownload;
  Error installUpdate(Cancellation&) override {++downloads;if(duringDownload)duringDownload();return result;}
  Error activateUpdate(bool) override {++activations;return activation;}
};
int main(){
  for(Error failure : {Error::None,Error::HashMismatch,Error::SignatureInvalid,Error::Cancelled,Error::FlashError,Error::DataBudget}){
    InstallPlatform p;FakeUart uart(p);auto config=sleepConfig();LTEUpdateManager manager(p,uart,config);
    CHECK(manager.begin()==Error::None);char reply[151];
    CHECK(manager.handleCommand("lte modem sleep",reply,sizeof(reply)));manager.run(p.queue.front());p.queue.pop_front();
    CHECK(manager.snapshot().sleepRequested);
    p.result=failure;
    CHECK(manager.handleCommand("start lte update ",reply,sizeof(reply)));CHECK(!manager.snapshot().simulated);
    manager.run(p.queue.front());p.queue.pop_front();
    CHECK(p.downloads==1);CHECK(p.activations==(failure==Error::None?1u:0u));
    CHECK(manager.snapshot().sleepRequested);CHECK(!uart.begun);CHECK(!manager.snapshot().busy);
    CHECK(manager.snapshot().error==failure);
  }
  {InstallPlatform p;FakeUart uart(p);auto c=sleepConfig();LTEUpdateManager m(p,uart,c);m.begin();char reply[151];
   p.duringDownload=[&]{m.handleCommand("lte update cancel",reply,sizeof(reply));};
   m.handleCommand("start lte update",reply,sizeof(reply));m.run(p.queue.front());
   CHECK(p.activations==0);CHECK(m.snapshot().error==Error::Cancelled);}
  {InstallPlatform p;FakeUart uart(p);auto c=sleepConfig();LTEUpdateManager m(p,uart,c);m.begin();char reply[151];
   m.handleCommand("lte modem sleep",reply,sizeof(reply));m.run(p.queue.front());p.queue.pop_front();
   p.duringDownload=[&]{uart.replies["AT+CSCLK=1"].push_back("\r\nERROR\r\n");};
   m.handleCommand("start lte update",reply,sizeof(reply));m.run(p.queue.front());
   CHECK(p.activations==0);CHECK(m.snapshot().cleanupError!=Error::None);}
  for(Error failure:{Error::None,Error::HashMismatch,Error::Cancelled}){
    InstallPlatform p;FakeUart uart(p);auto c=sleepConfig();c.wakeTimeoutMs=15000;p.allowFast=true;
    LTEUpdateManager m(p,uart,c);m.begin();char reply[151];
    p.result=failure;p.duringDownload=[&]{CHECK(uart.hostBaud==460800 && uart.modemBaud==460800);};
    m.handleCommand("start lte update",reply,sizeof(reply));m.run(p.queue.front());
    CHECK(uart.modemBaud==115200);CHECK(p.activations==(failure==Error::None?1u:0u));
  }
  {InstallPlatform p;FakeUart uart(p);auto c=sleepConfig();c.wakeTimeoutMs=15000;p.allowFast=true;
   LTEUpdateManager m(p,uart,c);m.begin();char reply[151];
   p.duringDownload=[&]{uart.replies["AT+IPR=115200"].push_back("\r\nERROR\r\n");};
   m.handleCommand("start lte update",reply,sizeof(reply));m.run(p.queue.front());
   CHECK(p.activations==0);CHECK(m.snapshot().cleanupError==Error::AtError);CHECK(!m.snapshot().sleepRequested);}
  {InstallPlatform p;FakeUart uart(p);auto c=sleepConfig();c.wakeTimeoutMs=15000;p.allowFast=true;
   LTEUpdateManager m(p,uart,c);m.begin();char reply[151];bool cancelled=false;
   uart.onCommand=[&](const std::string& cmd){if(!cancelled && cmd=="AT+IPR=460800"){cancelled=true;m.handleCommand("lte update cancel",reply,sizeof(reply));}};
   m.handleCommand("start lte update",reply,sizeof(reply));m.run(p.queue.front());
   CHECK(cancelled && p.downloads==0 && p.activations==0);CHECK(uart.modemBaud==115200);
   CHECK(m.snapshot().error==Error::Cancelled && m.snapshot().cleanupError==Error::None);}
  printf("PASS: %u install transaction checks; activation blocked on cancellation, validation error or cleanup failure\n",checks);
}
