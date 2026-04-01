#pragma once

#include <stddef.h>
#include <stdint.h>

enum class NmeaParseResult {
  NONE = 0,
  VALID_LINE,
  BAD_CHECKSUM,
  TOO_LONG,
};

class NmeaParser {
 public:
  NmeaParseResult ingest(uint8_t c, char* out_line, size_t out_len, bool* is_gga);

 private:
  bool validate_checksum_(const char* s) const;

  static const size_t kMaxLine = 128;
  char buf_[kMaxLine];
  size_t idx_ = 0;
};
