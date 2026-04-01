#include <Arduino.h>
#include <stdlib.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "gnss_uart.h"
#include "gnss_startup.h"
#include "log.h"
#include "mdns_link.h"
#include "nmea_parser.h"
#include "nmea_server.h"
#include "ntrip_client.h"
#include "status.h"
#include "wifi_link.h"

static GnssUart g_gnss;
static GnssStartup g_gnss_startup(&g_gnss);
static NmeaParser g_nmea_parser;
static NmeaServer g_nmea_server(NMEA_TCP_PORT);
static MdnsLink g_mdns;
static WifiLink g_wifi;
static NtripClient g_ntrip(NTRIP_HOST, NTRIP_PORT, NTRIP_MOUNTPOINT, NTRIP_USER,
                           NTRIP_PASS);

static char g_latest_gga[128];
static uint32_t g_last_health_log_ms = 0;
static bool g_prev_wifi_connected = false;
static bool g_prev_ntrip_connected = false;
static bool g_nmea_server_started = false;

static int parse_decimal_tenths(const char* s) {
  if (s == nullptr || s[0] == '\0') {
    return -1;
  }

  int value = 0;
  bool seen_digit = false;
  while (*s >= '0' && *s <= '9') {
    seen_digit = true;
    value = (value * 10) + (*s - '0');
    s++;
  }

  int tenths = value * 10;
  if (*s == '.') {
    s++;
    if (*s >= '0' && *s <= '9') {
      seen_digit = true;
      tenths += (*s - '0');
    }
  }

  return seen_digit ? tenths : -1;
}

static void parse_gga_status(const char* line) {
  if (line == nullptr || strstr(line, "GGA,") == nullptr) {
    return;
  }

  char copy[160];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = '\0';

  char* s = copy;
  if (s[0] == '$') {
    s++;
  }

  char* star = strchr(s, '*');
  if (star != nullptr) {
    *star = '\0';
  }

  int field = 0;
  int fix_quality = -1;
  int sats_used = -1;
  int hdop_tenths = -1;

  char* saveptr = nullptr;
  char* token = strtok_r(s, ",", &saveptr);
  while (token != nullptr) {
    field++;
    if (field == 7) {
      fix_quality = atoi(token);
    } else if (field == 8) {
      sats_used = atoi(token);
    } else if (field == 9) {
      hdop_tenths = parse_decimal_tenths(token);
    }
    token = strtok_r(nullptr, ",", &saveptr);
  }

  if (fix_quality >= 0) {
    g_status.gnss_fix_quality = static_cast<uint8_t>(fix_quality);
  }
  if (sats_used >= 0) {
    g_status.gnss_sats_used = static_cast<uint8_t>(sats_used);
  }
  if (hdop_tenths >= 0) {
    g_status.gnss_hdop_tenths = static_cast<int16_t>(hdop_tenths);
  }
}

static const char* fix_quality_text(uint8_t q) {
  switch (q) {
    case 0:
      return "NO_FIX";
    case 1:
      return "SPS";
    case 2:
      return "DGPS";
    case 4:
      return "RTK_FIX";
    case 5:
      return "RTK_FLOAT";
    case 6:
      return "DR";
    default:
      return "OTHER";
  }
}

static bool is_standard_nmea_sentence(const char* line) {
  if (line == nullptr || line[0] != '$') {
    return false;
  }
  if (line[1] == 'P') {
    return false;
  }
  return true;
}

static size_t rtcm_sink(const uint8_t* data, size_t len) {
  return g_gnss.write_bytes(data, len);
}

static void tick_gnss_rx(uint32_t now_ms) {
  char line[160];

  while (g_gnss.available() > 0) {
    int b = g_gnss.read();
    if (b < 0) {
      break;
    }

    g_status.gnss_rx_bytes++;

    bool is_gga = false;
    NmeaParseResult r = g_nmea_parser.ingest(static_cast<uint8_t>(b), line,
                                             sizeof(line), &is_gga);

    if (r == NmeaParseResult::NONE) {
      continue;
    }

    if (r == NmeaParseResult::BAD_CHECKSUM) {
      g_status.nmea_bad_checksum++;
      continue;
    }

    if (r == NmeaParseResult::TOO_LONG) {
      status_set_error("nmea: line too long");
      continue;
    }

    if (r == NmeaParseResult::VALID_LINE) {
      if (is_standard_nmea_sentence(line)) {
        g_status.nmea_lines_in++;
        g_status.last_nmea_ms = now_ms;
        g_nmea_server.send_line(line);
      }

      if (LOG_NMEA_RAW) {
        Serial.print(line);
      }

      if (is_gga) {
        strncpy(g_latest_gga, line, sizeof(g_latest_gga) - 1);
        g_latest_gga[sizeof(g_latest_gga) - 1] = '\0';
        parse_gga_status(line);
      }
    }
  }
}

static void tick_health(uint32_t now_ms) {
  if ((now_ms - g_last_health_log_ms) < HEALTH_LOG_INTERVAL_MS) {
    return;
  }
  g_last_health_log_ms = now_ms;

  g_status.wifi_connected = g_wifi.is_connected();
  g_status.ntrip_connected = g_ntrip.is_streaming();
  g_status.qfield_client_connected = g_nmea_server.has_client();

  if (g_status.wifi_connected != g_prev_wifi_connected) {
    g_prev_wifi_connected = g_status.wifi_connected;
    if (g_status.wifi_connected) {
      LOGI("Wi-Fi up: ip=%s rssi=%d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      LOGW("Wi-Fi down");
      status_set_error("wifi: disconnected");
    }
  }

  if (g_status.ntrip_connected != g_prev_ntrip_connected) {
    g_prev_ntrip_connected = g_status.ntrip_connected;
    if (g_status.ntrip_connected) {
      LOGI("NTRIP streaming active");
    } else {
      LOGW("NTRIP not streaming");
    }
  }

  const uint32_t nmea_age_s =
      (g_status.last_nmea_ms == 0) ? 0 : ((now_ms - g_status.last_nmea_ms) / 1000);
  const uint32_t rtcm_age_s =
      (g_status.last_rtcm_ms == 0) ? 0 : ((now_ms - g_status.last_rtcm_ms) / 1000);

  int hdop_whole = -1;
  int hdop_frac = 0;
  if (g_status.gnss_hdop_tenths >= 0) {
    hdop_whole = g_status.gnss_hdop_tenths / 10;
    hdop_frac = g_status.gnss_hdop_tenths % 10;
  }

  LOGI("st wifi=%d mdns=%d ntrip=%d qf=%d rtcm=%lu rx=%lu nmea_in=%lu out=%lu bad=%lu "
       "fix=%s sat=%u hdop=%d.%d age_nmea=%lus age_rtcm=%lus err=%s",
       static_cast<int>(g_status.wifi_connected),
       static_cast<int>(g_mdns.is_started()),
       static_cast<int>(g_status.ntrip_connected),
       static_cast<int>(g_status.qfield_client_connected),
       static_cast<unsigned long>(g_status.rtcm_bytes_in),
       static_cast<unsigned long>(g_status.gnss_rx_bytes),
       static_cast<unsigned long>(g_status.nmea_lines_in),
       static_cast<unsigned long>(g_status.nmea_lines_out),
       static_cast<unsigned long>(g_status.nmea_bad_checksum),
       fix_quality_text(g_status.gnss_fix_quality),
       static_cast<unsigned>(g_status.gnss_sats_used), hdop_whole, hdop_frac,
       static_cast<unsigned long>(nmea_age_s), static_cast<unsigned long>(rtcm_age_s),
       g_status.last_error);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  status_init();
  memset(g_latest_gga, 0, sizeof(g_latest_gga));

  LOGI("ESP32 GNSS/RTK rover prototype booting");

  g_gnss.begin(GNSS_UART_NUM, GNSS_BAUD, GNSS_RX_PIN, GNSS_TX_PIN);
  g_gnss_startup.begin(millis());
  g_wifi.begin(WIFI_SSID, WIFI_PASS);
  g_mdns.begin(MDNS_HOSTNAME, MDNS_INSTANCE, NMEA_TCP_PORT);
  g_ntrip.set_rtcm_sink(rtcm_sink);
}

void loop() {
  const uint32_t now_ms = millis();

  g_wifi.tick(now_ms);
  if (MDNS_ENABLE) {
    g_mdns.tick(g_wifi.is_connected());
  }

  if (!g_nmea_server_started && g_wifi.is_connected()) {
    g_nmea_server.begin();
    g_nmea_server_started = true;
  }

  if (g_nmea_server_started) {
    g_nmea_server.tick();
  }

  g_gnss_startup.tick(now_ms);
  tick_gnss_rx(now_ms);
  g_ntrip.tick(now_ms, g_wifi.is_connected(), g_latest_gga);
  tick_health(now_ms);

  delay(2);
}
