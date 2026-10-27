#include "UpdateEndpoint.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
using namespace lte;
static unsigned checks=0;
#define CHECK(value) do{++checks;if(!(value)){fprintf(stderr,"FAIL endpoint line %d\n",__LINE__);exit(1);}}while(0)
int main(){
  UpdateEndpoint endpoint;char url[360];
  CHECK(!endpoint.apiUrl(url,sizeof(url),"manifest",""));
  for(const char* value:{"https://updates.example.invalid","https://updates.example.invalid/meshcoreupdate", "https://updates.example.invalid:8443/updates/v2", "https://192.0.2.1/update", "https://xn--example-9d0b.invalid/update"}){
    CHECK(endpoint.assign(value));CHECK(!strcmp(endpoint.value(),value));
    CHECK(endpoint.apiUrl(url,sizeof(url),"manifest","?key=fixture"));
    CHECK(std::string(url)==std::string(value)+"/api/v1/manifest?key=fixture");
    CHECK(endpoint.apiUrl(url,sizeof(url),"report","?key=fixture"));
    CHECK(std::string(url)==std::string(value)+"/api/v1/report?key=fixture");
  }
  CHECK(endpoint.assign("https://updates.example.invalid/meshcoreupdate/  \t"));
  CHECK(!strcmp(endpoint.value(),"https://updates.example.invalid/meshcoreupdate"));
  const std::string saved=endpoint.value();
  for(const char* invalid:{"", "http://updates.example.invalid", "https://", "https:///path", "https://user:password@example.invalid/update", "https://example.invalid?query", "https://example.invalid/path?query", "https://example.invalid/path#fragment", "https://example.invalid/\"", "https://example.invalid/\\path", "https://example.invalid/%2e%2e", "https://example.invalid/../update", "https://example.invalid/./update", "https://example.invalid//path", "https://example.invalid:0/path", "https://example.invalid:65536/path", "https://example.invalid:/path", "https://example.invalid:443evil/path", "https://-bad.invalid/path", "https://bad-.invalid/path", "https://bad..invalid/path", "https://bad_name.invalid/path", "https://example.invalid/line\r\nAT+HTTPACTION=0", "https://[::1]/path"}){
    CHECK(!endpoint.assign(invalid));CHECK(saved==endpoint.value());
  }
  std::string maximum="https://updates.example.invalid/";maximum+=std::string(UpdateEndpoint::MaxLength-maximum.size(),'x');
  CHECK(endpoint.assign(maximum.c_str()));CHECK(!endpoint.assign((maximum+"x").c_str()));
  UpdateEndpoint snapshot=endpoint;CHECK(endpoint.assign("https://other.example.invalid/new"));CHECK(snapshot.value()==maximum);
  CHECK(endpoint.apiUrl(url,sizeof(url),"firmware/0123456789abcdef01234567","?key=fixture"));
  CHECK(std::string(url)=="https://other.example.invalid/new/api/v1/firmware/0123456789abcdef01234567?key=fixture");
  CHECK(!endpoint.apiUrl(url,sizeof(url),"firmware/../../../../../../../../",""));
  CHECK(!endpoint.apiUrl(url,sizeof(url),"https://untrusted.example.invalid",""));
  CHECK(!endpoint.apiUrl(url,4,"manifest",""));CHECK(url[0]==0);
  CHECK(!endpoint.assign(nullptr));
  printf("PASS: %u update endpoint checks; HTTPS, routing, injection rejection, limits and snapshots\n",checks);
}
