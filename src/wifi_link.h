#pragma once

#include <stdint.h>

class WifiLink {
 public:
  void begin(const char* ssid, const char* pass);
  void tick(uint32_t now_ms);
  bool is_connected() const;

 private:
  void connect_now_(uint32_t now_ms);

  const char* ssid_ = nullptr;
  const char* pass_ = nullptr;
  uint32_t last_attempt_ms_ = 0;
};
