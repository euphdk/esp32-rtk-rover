#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gnss_uart.h"

class GnssStartup {
 public:
  explicit GnssStartup(GnssUart* uart) : uart_(uart) {}

  void begin(uint32_t now_ms);
  void tick(uint32_t now_ms);
  bool completed() const;

 private:
  size_t send_command_(const char* body);

  GnssUart* uart_ = nullptr;
  bool active_ = false;
  uint32_t start_ms_ = 0;
  uint32_t last_send_ms_ = 0;
  size_t next_index_ = 0;
};
