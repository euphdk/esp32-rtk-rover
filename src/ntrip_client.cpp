#include "ntrip_client.h"

#include <string.h>

#include "config.h"
#include "log.h"
#include "status.h"

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static String base64_encode(const char* in) {
  String out;
  if (in == nullptr) {
    return out;
  }
  size_t len = strlen(in);
  out.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t octet_a = static_cast<uint8_t>(in[i]);
    uint32_t octet_b = (i + 1 < len) ? static_cast<uint8_t>(in[i + 1]) : 0;
    uint32_t octet_c = (i + 2 < len) ? static_cast<uint8_t>(in[i + 2]) : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    out += kB64Table[(triple >> 18) & 0x3F];
    out += kB64Table[(triple >> 12) & 0x3F];
    out += (i + 1 < len) ? kB64Table[(triple >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? kB64Table[triple & 0x3F] : '=';
  }
  return out;
}

NtripClient::NtripClient(const char* host, uint16_t port, const char* mountpoint,
                         const char* user, const char* pass)
    : host_(host),
      port_(port),
      mountpoint_(mountpoint),
      user_(user),
      pass_(pass) {}

void NtripClient::set_rtcm_sink(RtcmSinkFn sink) { sink_ = sink; }

String NtripClient::make_auth_header_() const {
  String auth_input;
  auth_input.reserve(128);
  auth_input += (user_ != nullptr) ? user_ : "";
  auth_input += ":";
  auth_input += (pass_ != nullptr) ? pass_ : "";
  return base64_encode(auth_input.c_str());
}

bool NtripClient::connect_and_request_(uint32_t now_ms) {
  if (host_ == nullptr || mountpoint_ == nullptr || host_[0] == '\0' ||
      mountpoint_[0] == '\0') {
    status_set_error("ntrip: invalid host/mount");
    return false;
  }

  const char* mp = mountpoint_;
  if (mp[0] == '/') {
    mp++;
  }

  LOGI("NTRIP connect %s:%u/%s", host_, port_, mp);
  client_.setTimeout(2);

  if (!client_.connect(host_, port_)) {
    status_set_error("ntrip: socket connect failed");
    return false;
  }

  String req;
  req.reserve(512);
  req += "GET /";
  req += mp;
  req += " HTTP/1.1\r\n";
  req += "Host: ";
  req += host_;
  req += ":";
  req += String(port_);
  req += "\r\n";
  req += "Ntrip-Version: Ntrip/2.0\r\n";
  req += "User-Agent: NTRIP ESP32-Rover/0.1\r\n";
  req += "Accept: */*\r\n";
  req += "Connection: keep-alive\r\n";
  req += "Authorization: Basic ";
  req += make_auth_header_();
  req += "\r\n\r\n";

  LOGI("NTRIP request sent");
  client_.print(req);

  state_ = State::WAIT_HEADER;
  header_start_ms_ = now_ms;
  header_len_ = 0;
  memset(header_buf_, 0, sizeof(header_buf_));
  return true;
}

void NtripClient::close_with_backoff_(const char* reason, uint32_t now_ms,
                                      bool increase_backoff) {
  if (client_) {
    client_.stop();
  }
  state_ = State::DISCONNECTED;

  if (increase_backoff) {
    if (retry_backoff_ms_ == 0) {
      retry_backoff_ms_ = NTRIP_RETRY_MIN_MS;
    } else {
      retry_backoff_ms_ *= 2;
      if (retry_backoff_ms_ > NTRIP_RETRY_MAX_MS) {
        retry_backoff_ms_ = NTRIP_RETRY_MAX_MS;
      }
    }
  } else {
    retry_backoff_ms_ = NTRIP_RETRY_MIN_MS;
  }

  next_retry_ms_ = now_ms + retry_backoff_ms_;
  status_set_error(reason);
  LOGW("NTRIP closed: %s (retry in %lu ms)", reason,
       static_cast<unsigned long>(retry_backoff_ms_));
}

bool NtripClient::parse_header_if_ready_(uint32_t now_ms) {
  while (client_.available() > 0 && header_len_ < (sizeof(header_buf_) - 1)) {
    header_buf_[header_len_++] = static_cast<char>(client_.read());
    header_buf_[header_len_] = '\0';
    const bool has_header_end =
        (strstr(header_buf_, "\r\n\r\n") != nullptr) ||
        (strstr(header_buf_, "\n\n") != nullptr);

    // Some NTRIP v1 casters send "ICY 200 OK\r\n" then start RTCM stream
    // immediately, without a full HTTP-style header block.
    const bool is_icy_line_done =
        (strstr(header_buf_, "ICY 200 OK") != nullptr) &&
        (strchr(header_buf_, '\n') != nullptr);

    if (has_header_end || is_icy_line_done) {
      if (strstr(header_buf_, "200") != nullptr || strstr(header_buf_, "ICY 200") != nullptr) {
        LOGI("NTRIP streaming started");
        state_ = State::STREAMING;
        retry_backoff_ms_ = NTRIP_RETRY_MIN_MS;
        last_data_ms_ = now_ms;
        return true;
      }

      if (strstr(header_buf_, "401") != nullptr || strstr(header_buf_, "403") != nullptr) {
        close_with_backoff_("ntrip: auth failed", now_ms, true);
        return false;
      }

      LOGW("NTRIP header: %s", header_buf_);
      close_with_backoff_("ntrip: bad response", now_ms, true);
      return false;
    }
  }

  if ((now_ms - header_start_ms_) > NTRIP_HEADER_TIMEOUT_MS) {
    if (header_len_ > 0) {
      LOGW("NTRIP partial header: %s", header_buf_);
    } else {
      LOGW("NTRIP no header bytes received before timeout");
    }
    close_with_backoff_("ntrip: header timeout", now_ms, true);
    return false;
  }

  return false;
}

void NtripClient::maybe_send_gga_(uint32_t now_ms, const char* latest_gga_line) {
  if (!NTRIP_SEND_GGA || latest_gga_line == nullptr || latest_gga_line[0] == '\0') {
    return;
  }

  if ((now_ms - last_gga_sent_ms_) < NTRIP_GGA_INTERVAL_MS) {
    return;
  }

  size_t n = client_.print(latest_gga_line);
  if (n > 0) {
    last_gga_sent_ms_ = now_ms;
    LOGI("NTRIP GGA uplink sent");
  }
}

void NtripClient::tick(uint32_t now_ms, bool wifi_ok, const char* latest_gga_line) {
  if (host_ == nullptr || mountpoint_ == nullptr || host_[0] == '\0' ||
      mountpoint_[0] == '\0') {
    return;
  }

  if (!wifi_ok) {
    if (state_ != State::DISCONNECTED) {
      close_with_backoff_("ntrip: wifi down", now_ms, false);
    }
    return;
  }

  if (state_ == State::DISCONNECTED) {
    if (now_ms < next_retry_ms_) {
      return;
    }

    if (!connect_and_request_(now_ms)) {
      close_with_backoff_("ntrip: connect retry", now_ms, true);
      return;
    }
    return;
  }

  if (!client_.connected()) {
    close_with_backoff_("ntrip: socket dropped", now_ms, true);
    return;
  }

  if (state_ == State::WAIT_HEADER) {
    parse_header_if_ready_(now_ms);
    return;
  }

  if (state_ != State::STREAMING) {
    return;
  }

  maybe_send_gga_(now_ms, latest_gga_line);

  static const size_t kMaxRtcmBytesPerTick = 2048;
  size_t processed = 0;
  uint8_t buf[256];

  while (client_.available() > 0 && processed < kMaxRtcmBytesPerTick) {
    int avail = client_.available();
    if (avail <= 0) {
      break;
    }

    size_t to_read = static_cast<size_t>(avail);
    if (to_read > sizeof(buf)) {
      to_read = sizeof(buf);
    }

    int n = client_.read(buf, to_read);
    if (n <= 0) {
      break;
    }

    processed += static_cast<size_t>(n);
    last_data_ms_ = now_ms;
    g_status.rtcm_bytes_in += static_cast<uint32_t>(n);
    g_status.last_rtcm_ms = now_ms;

    if (sink_ != nullptr) {
      sink_(buf, static_cast<size_t>(n));
    }
  }

  if ((now_ms - last_data_ms_) > NTRIP_STALE_DATA_TIMEOUT_MS) {
    close_with_backoff_("ntrip: stale stream", now_ms, true);
  }
}

bool NtripClient::is_streaming() const { return state_ == State::STREAMING; }
