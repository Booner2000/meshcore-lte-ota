#pragma once
#include "LTEUpdateTypes.h"

namespace lte {
struct FirmwareManifest {
  const char* target = nullptr;
  uint32_t imageSize = 0;
  uint32_t releaseCounter = 0;
  bool hasLteOta = false;
  uint8_t sha256[32] = {};
};
class FirmwareDownloader {
public:
  Error fetchManifest(FirmwareManifest&) { return Error::NotImplemented; }
  Error verifySignature(const FirmwareManifest&) { return Error::NotImplemented; }
  Error checkCompatibility(const FirmwareManifest&) { return Error::NotImplemented; }
  Error download(const FirmwareManifest&) { return Error::NotImplemented; }
};
}
