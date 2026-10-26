#pragma once

namespace lte {
// Match the complete command, allowing terminal whitespace but no extra arguments.
inline bool commandEquals(const char* command, const char* expected) {
  if (!command || !expected) return false;
  while (*expected) {
    if (*command++ != *expected++) return false;
  }
  while (*command == ' ' || *command == '\t' || *command == '\r' || *command == '\n') ++command;
  return *command == '\0';
}
}
