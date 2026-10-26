#define main phaseOneTests
#include "../lte_phase1/test_phase1.cpp"
#undef main
#include "A7670BodyReader.h"
struct CancelFlag : Cancellation {bool stop=false;bool cancelled() override{return stop;}};
int main(){
  const std::string payload("\0\r\nOK\xff",6);
  const std::string body="\r\n+HTTPREAD: 6\r\n"+payload+"\r\n+HTTPREAD: 0\r\n";
  for(bool initialOk : {false,true}){
    FakePlatform p;FakeUart uart(p);CancelFlag cancel;uint8_t data[6]={};
    uart.replies["AT+HTTPREAD=6"].push_back(initialOk?"OK"+body:body+"OK\r\n");
    CHECK(readBufferedHttpBody(uart,p,cancel,1024,data,6)==Error::None);
    CHECK(!memcmp(data,payload.data(),6));CHECK(uart.commands.size()==1);CHECK(uart.commands[0]=="AT+HTTPREAD=6");
  }
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];
   uart.replies["AT+HTTPREAD=6"]={"ERROR\r\n","ERROR\r\n","ERROR\r\n","OK"+body};
   CHECK(readBufferedHttpBody(uart,p,c,0,data,6)==Error::None);CHECK(uart.commands.size()==4);
   CHECK(!memcmp(data,payload.data(),6));for(auto& command:uart.commands)CHECK(command=="AT+HTTPREAD=6");}
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];
   uart.replies["AT+HTTPREAD=6"]={"ERROR\r\n","ERROR\r\n","ERROR\r\n","ERROR\r\n"};
   CHECK(readBufferedHttpBody(uart,p,c,0,data,6)==Error::BadResponse);CHECK(uart.commands.size()==4);CHECK(p.clock<1000);}
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];uart.replies["AT+HTTPREAD=6"]={"ERROR\r\n"};
   p.onWait=[&]{c.stop=true;};CHECK(readBufferedHttpBody(uart,p,c,0,data,6)==Error::Cancelled);CHECK(uart.commands.size()==1);}
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];uart.replies["AT+HTTPREAD=6"]={"OK\r\n+HTTPREAD: 7\r\n"};
   CHECK(readBufferedHttpBody(uart,p,c,0,data,6)==Error::BadResponse);CHECK(uart.commands.size()==1);}
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];uart.replies["AT+HTTPREAD=6"]={std::string(220,'X')};
   CHECK(readBufferedHttpBody(uart,p,c,0,data,6)==Error::ResponseOverflow);}
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];uart.replies["AT+HTTPREAD=6"]={"OK\r\n+HTTPREAD: 6\r\n"};
   CHECK(readBufferedHttpBody(uart,p,c,0,data,6)==Error::AtTimeout);CHECK(uart.commands.size()==1);}
  {FakePlatform p;FakeUart uart(p);CancelFlag c;uint8_t data[6];
   CHECK(readBufferedHttpBody(uart,p,c,65535,data,6)==Error::BadConfig);CHECK(uart.commands.empty());}
  printf("PASS: %u HTTP buffered reader checks; binary framing, local retries, cancellation and bounds\n",checks);
}
