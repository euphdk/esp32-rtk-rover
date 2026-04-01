#pragma once

#include <stdint.h>

struct RoverConfig {
  char wifi_ssid[33];
  char wifi_pass[65];

  // Future-ready NTRIP fields.
  char ntrip_host[64];
  uint16_t ntrip_port;
  char ntrip_mountpoint[64];
  char ntrip_user[64];
  char ntrip_pass[64];
};

void config_store_load(RoverConfig* cfg, bool* loaded_from_nvs);
bool config_store_save_wifi(RoverConfig* cfg, const char* ssid, const char* pass);
