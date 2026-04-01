#pragma once

#include <stdint.h>

#include "config_store.h"

class ApPortal {
 public:
  void begin(RoverConfig* cfg);
  void tick(uint32_t now_ms, bool wifi_connected);
  bool ap_active() const;

 private:
  void start_ap_();
  void stop_ap_();
  void start_server_();
  void stop_server_();

  RoverConfig* cfg_ = nullptr;
  bool ap_active_ = false;
  bool server_active_ = false;
  bool prev_wifi_connected_ = false;
  uint32_t wifi_down_since_ms_ = 0;
};
