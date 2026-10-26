#pragma once
#include "LTEUpdatePlatform.h"
namespace lte {
// Reads the sequential READMODE=0 buffer. Retries never issue HTTPACTION/LTE GET.
Error readBufferedHttpBody(ByteUart& uart, Platform& platform, Cancellation& cancel,
                        uint32_t offset, uint8_t* data, size_t count);
}
