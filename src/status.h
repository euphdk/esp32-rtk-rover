#pragma once

#include <stdint.h>

struct RoverStatus {
  bool wifi_connected;
  bool ap_active;
  bool mdns_started;
  bool cfg_from_nvs;
  bool ntrip_connected;
  bool qfield_client_connected;

  uint32_t rtcm_bytes_in;
  uint32_t gnss_rx_bytes;
  uint32_t nmea_lines_in;
  uint32_t nmea_lines_out;
  uint32_t nmea_bad_checksum;
  uint32_t nmea_too_long;

  uint8_t gnss_fix_quality;
  uint8_t gnss_sats_used;
  int16_t gnss_hdop_tenths;

  uint32_t last_rtcm_ms;
  uint32_t last_nmea_ms;
  uint32_t last_qfield_tx_ms;

  uint16_t gnss_uart_overflow_count;
  uint16_t gnss_uart_frame_err_count;

  char last_error[96];
};

extern RoverStatus g_status;

void status_init();
void status_set_error(const char* msg);
