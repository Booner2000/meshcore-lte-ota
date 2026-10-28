#pragma once
#include <atomic>
#include <stdint.h>
#include <stdio.h>
namespace lte {
// Deliberately numeric-only: no URL, identity or configuration secret can be stored.
struct WifiDiagnostics {
  std::atomic<unsigned> stage{0},phase{0};
  std::atomic<int> http{0},tls{0},rssi{0};
  std::atomic<uint32_t> received{0},expected{0},elapsed{0};
  void format(char* reply,size_t capacity) const {
    if(!reply || !capacity)return;
    static const char* const stages[]={"none","manifest","firmware","report"};
    static const char* const phases[]={"none","headers","body","complete"};
    const unsigned s=stage.load(),p=phase.load();
    snprintf(reply,capacity,"WIFI_DIAG stage=%s phase=%s http=%d tls_raw=%d rx=%lu/%lu ms=%lu rssi=%d",
      stages[s<4?s:0],phases[p<4?p:0],http.load(),tls.load(),
      static_cast<unsigned long>(received.load()),static_cast<unsigned long>(expected.load()),
      static_cast<unsigned long>(elapsed.load()),rssi.load());
  }
};
}
