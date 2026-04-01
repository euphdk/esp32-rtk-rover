#include "nmea_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int hex_to_int(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool NmeaParser::validate_checksum_(const char* s) const {
  if (s == nullptr || s[0] != '$') {
    return false;
  }

  const char* star = strchr(s, '*');
  if (star == nullptr || (star - s) < 1) {
    return false;
  }
  if (star[1] == '\0' || star[2] == '\0') {
    return false;
  }

  uint8_t calc = 0;
  for (const char* p = s + 1; p < star; ++p) {
    calc ^= static_cast<uint8_t>(*p);
  }

  int h1 = hex_to_int(star[1]);
  int h2 = hex_to_int(star[2]);
  if (h1 < 0 || h2 < 0) {
    return false;
  }

  uint8_t expected = static_cast<uint8_t>((h1 << 4) | h2);
  return calc == expected;
}

NmeaParseResult NmeaParser::ingest(uint8_t c, char* out_line, size_t out_len,
                                   bool* is_gga) {
  if (is_gga != nullptr) {
    *is_gga = false;
  }

  if (c == '\r') {
    return NmeaParseResult::NONE;
  }

  if (c != '\n') {
    if (idx_ >= (kMaxLine - 1)) {
      idx_ = 0;
      return NmeaParseResult::TOO_LONG;
    }
    buf_[idx_++] = static_cast<char>(c);
    return NmeaParseResult::NONE;
  }

  if (idx_ == 0) {
    return NmeaParseResult::NONE;
  }

  buf_[idx_] = '\0';
  idx_ = 0;

  if (!validate_checksum_(buf_)) {
    return NmeaParseResult::BAD_CHECKSUM;
  }

  if (out_line != nullptr && out_len > 0) {
    snprintf(out_line, out_len, "%s\r\n", buf_);
  }

  if (is_gga != nullptr) {
    // Handles $GPGGA, $GNGGA, and similar talkers.
    if (strstr(buf_, "GGA,") != nullptr) {
      *is_gga = true;
    }
  }

  return NmeaParseResult::VALID_LINE;
}
