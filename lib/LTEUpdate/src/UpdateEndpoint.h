#pragma once
#include <stddef.h>
namespace lte {
class UpdateEndpoint {
  char base[113] = {};
public:
  // Fits the existing radio CLI command and reply limits, including the prefix.
  static constexpr size_t MaxLength = 112;
  bool assign(const char* value);
  const char* value() const { return base; }
  bool apiUrl(char* output, size_t capacity, const char* route, const char* identity) const;
};
}
