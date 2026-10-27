#pragma once
#include "LTEUpdateTypes.h"
namespace lte {
// Arduino-ESP32 2.0.17 start_ssl_client returns -1 for socket errors and its
// handshake deadline. On success it returns the nonnegative socket descriptor;
// WiFiClientSecure::lastError retains that value even after a later HTTP timeout.
// Actual certificate/CA failures retain negative mbedTLS error codes.
inline Error wifiConnectFailure(int error) {
  return error>=0 || error==-1 ? Error::NetworkUnavailable : Error::TlsConfig;
}
}
