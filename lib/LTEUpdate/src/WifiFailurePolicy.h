#pragma once
#include "LTEUpdateTypes.h"
namespace lte {
// Arduino-ESP32 2.0.17 start_ssl_client returns -1 for socket errors and its
// handshake deadline. Actual certificate/CA failures retain mbedTLS error codes.
inline Error wifiConnectFailure(int error) {
  return error==0 || error==-1 ? Error::NetworkUnavailable : Error::TlsConfig;
}
}
