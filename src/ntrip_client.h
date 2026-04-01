#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stddef.h>
#include <stdint.h>

typedef size_t (*RtcmSinkFn)(const uint8_t* data, size_t len);

class NtripClient {
 public:
  NtripClient(const char* host, uint16_t port, const char* mountpoint,
              const char* user, const char* pass);

  void set_rtcm_sink(RtcmSinkFn sink);
  void tick(uint32_t now_ms, bool wifi_ok, const char* latest_gga_line);
  bool is_streaming() const;

 private:
  enum class State {
    DISCONNECTED = 0,
    WAIT_HEADER,
    STREAMING,
  };

  bool connect_and_request_(uint32_t now_ms);
  void close_with_backoff_(const char* reason, uint32_t now_ms, bool increase_backoff);
  bool parse_header_if_ready_(uint32_t now_ms);
  void maybe_send_gga_(uint32_t now_ms, const char* latest_gga_line);

  String make_auth_header_() const;

  const char* host_;
  uint16_t port_;
  const char* mountpoint_;
  const char* user_;
  const char* pass_;

  WiFiClient client_;
  RtcmSinkFn sink_ = nullptr;

  State state_ = State::DISCONNECTED;
  uint32_t next_retry_ms_ = 0;
  uint32_t retry_backoff_ms_ = 0;
  uint32_t header_start_ms_ = 0;
  uint32_t last_data_ms_ = 0;
  uint32_t last_gga_sent_ms_ = 0;

  char header_buf_[512];
  size_t header_len_ = 0;
};
