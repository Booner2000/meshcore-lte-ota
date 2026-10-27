#include "UpdateEndpoint.h"
#include <stdio.h>
#include <string.h>
namespace lte {
namespace {
bool alnum(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
bool whitespace(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
bool pathCharacter(char c){return alnum(c)||c=='-'||c=='_'||c=='.'||c=='~';}
}
bool UpdateEndpoint::assign(const char* input){
  if(!input)return false;
  while(whitespace(*input))++input;
  size_t length=strlen(input);
  while(length&&whitespace(input[length-1]))--length;
  if(length>MaxLength || length<9 || strncmp(input,"https://",8))return false;
  const char* end=input+length;const char* host=input+8;const char* cursor=host;
  // DNS/IPv4 and an optional decimal port; no userinfo, IPv6 literals or escapes.
  while(cursor<end && *cursor!='/' && *cursor!=':')++cursor;
  if(cursor==host || !alnum(*host) || !alnum(cursor[-1]))return false;
  size_t label=0;
  for(const char* p=host;p<cursor;++p){
    if(*p=='.'){if(!label || p[-1]=='-')return false;label=0;}
    else {if(!alnum(*p)&&*p!='-')return false;if(!label&&*p=='-')return false;if(++label>63)return false;}
  }
  if(cursor<end && *cursor==':'){
    ++cursor;unsigned port=0,digits=0;
    while(cursor<end && *cursor!='/'){
      if(*cursor<'0'||*cursor>'9'||++digits>5)return false;
      port=port*10+unsigned(*cursor++-'0');
    }
    if(!digits || !port || port>65535)return false;
  }
  while(cursor<end){
    if(*cursor++!='/')return false;
    const char* segment=cursor;
    while(cursor<end && *cursor!='/'){if(!pathCharacter(*cursor++))return false;}
    const size_t size=cursor-segment;
    if((size==1&&segment[0]=='.')||(size==2&&segment[0]=='.'&&segment[1]=='.'))return false;
    if(!size && cursor<end)return false;
  }
  if(length>8 && input[length-1]=='/')--length;
  memcpy(base,input,length);base[length]=0;return true;
}
bool UpdateEndpoint::apiUrl(char* output,size_t capacity,const char* route,const char* identity)const{
  if(!output||!capacity)return false;
  output[0]=0;
  if(!base[0]||!route||!identity)return false;
  // Routes are local constants or an artifact ID checked as lowercase hex.
  if(strcmp(route,"manifest") && strcmp(route,"report")){
    if(strncmp(route,"firmware/",9)||strlen(route)!=33)return false;
    for(const char* p=route+9;*p;++p)if(!(*p>='0'&&*p<='9')&&!(*p>='a'&&*p<='f'))return false;
  }
  const int count=snprintf(output,capacity,"%s/api/v1/%s%s",base,route,identity);
  if(count<0||static_cast<size_t>(count)>=capacity){output[0]=0;return false;}
  return true;
}
}
