#include "A7670BodyReader.h"
#include <stdio.h>
#include <string.h>
namespace lte {
Error readBufferedHttpBody(ByteUart& uart, Platform& platform, Cancellation& cancel,
                        uint32_t offset, uint8_t* data, size_t count) {
  if(!data || !count || count>2048 || offset>65536 || count>65536-offset)return Error::BadConfig;
  char command[64],line[192];
  snprintf(command,sizeof(command),"AT+HTTPREAD=%u",unsigned(count));
  const auto send=[&]()->bool {uart.discardInput();return uart.write(command,strlen(command)) && uart.write("\r",1);};
  if(!send())return Error::UartIo;
  const uint32_t started=platform.nowMs();
  const auto receive=[&](uint32_t timeout)->Error {
    const uint32_t start=platform.nowMs();size_t used=0;
    while(platform.nowMs()-start<timeout){
      if(cancel.cancelled())return Error::Cancelled;
      Error fault=uart.fault();if(fault!=Error::None)return fault;
      char c;if(!uart.read(c,10))continue;
      if(c=='\r')continue;
      if(c=='\n'){if(!used)continue;line[used]=0;return Error::None;}
      if(used+1>=sizeof(line))return Error::ResponseOverflow;
      line[used++]=c;
    }
    return Error::AtTimeout;
  };
  unsigned localRetries=0,length=0;bool sawOk=false;
  for(;;){
    Error e=receive(5000);if(e!=Error::None)return e;
    if(!strcmp(line,"OK"))sawOk=true;
    if(sscanf(line,"+HTTPREAD: %u",&length)==1 && length>0)break;
    if(!strcmp(line,"ERROR")){
      if(localRetries>=3)return Error::BadResponse;
      platform.log("HTTPREAD not ready; retry unread buffer without LTE transfer");
      platform.waitMs(100u << localRetries++);sawOk=false;
      if(cancel.cancelled())return Error::Cancelled;
      if(!send())return Error::UartIo;
    }
    if(platform.nowMs()-started>15000)return Error::AtTimeout;
  }
  if(length!=count)return Error::BadResponse;
  size_t used=0;
  while(used<count && platform.nowMs()-started<20000){
    if(cancel.cancelled())return Error::Cancelled;
    Error fault=uart.fault();if(fault!=Error::None)return fault;
    char c;if(uart.read(c,10))data[used++]=static_cast<uint8_t>(c);
  }
  if(used!=count)return Error::AtTimeout;
  Error e=receive(5000);if(e!=Error::None)return e;
  if(strcmp(line,"+HTTPREAD: 0"))return Error::BadResponse;
  if(!sawOk){e=receive(5000);if(e!=Error::None)return e;if(strcmp(line,"OK"))return Error::BadResponse;}
  return Error::None;
}
}
