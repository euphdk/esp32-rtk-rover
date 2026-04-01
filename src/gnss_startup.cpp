#include "gnss_startup.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "log.h"

static uint8_t nmea_checksum(const char* s) {
  uint8_t csum = 0;
  if (s == nullptr) {
    return csum;
  }
  while (*s != '\0') {
    csum ^= static_cast<uint8_t>(*s++);
  }
  return csum;
}

void GnssStartup::begin(uint32_t now_ms) {
  next_index_ = 0;
  start_ms_ = now_ms;
  last_send_ms_ = 0;
  active_ = GNSS_SEND_STARTUP_CONFIG && (GNSS_STARTUP_COMMAND_COUNT > 0);

  if (active_) {
    LOGI("GNSS startup config enabled (%u commands)",
         static_cast<unsigned>(GNSS_STARTUP_COMMAND_COUNT));
  } else {
    LOGI("GNSS startup config disabled");
  }
}

size_t GnssStartup::send_command_(const char* body) {
  if (uart_ == nullptr || body == nullptr || body[0] == '\0') {
    return 0;
  }

  char framed[192];
  const uint8_t csum = nmea_checksum(body);
  snprintf(framed, sizeof(framed), "$%s*%02X\r\n", body, csum);
  LOGI("GNSS CFG > %s", framed);
  return uart_->write_line(framed);
}

void GnssStartup::tick(uint32_t now_ms) {
  if (!active_) {
    return;
  }

  if ((now_ms - start_ms_) < GNSS_STARTUP_DELAY_MS) {
    return;
  }

  if (last_send_ms_ != 0 && ((now_ms - last_send_ms_) < GNSS_CMD_INTERVAL_MS)) {
    return;
  }

  if (next_index_ >= GNSS_STARTUP_COMMAND_COUNT) {
    active_ = false;
    LOGI("GNSS startup config complete");
    return;
  }

  send_command_(GNSS_STARTUP_COMMANDS[next_index_]);
  next_index_++;
  last_send_ms_ = now_ms;
}

bool GnssStartup::completed() const { return !active_; }
