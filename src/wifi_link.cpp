#include "wifi_link.h"

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "log.h"
#include "status.h"

void WifiLink::begin(const char* ssid, const char* pass) {
  ssid_ = ssid;
  pass_ = pass;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  connect_now_(millis());
}

void WifiLink::connect_now_(uint32_t now_ms) {
  if (ssid_ == nullptr || ssid_[0] == '\0') {
    status_set_error("wifi: empty ssid");
    return;
  }

  last_attempt_ms_ = now_ms;
  WiFi.disconnect();
  WiFi.begin(ssid_, pass_);
  LOGI("Wi-Fi connecting: ssid=%s", ssid_);
}

void WifiLink::tick(uint32_t now_ms) {
  if (is_connected()) {
    return;
  }

  if ((now_ms - last_attempt_ms_) >= WIFI_RETRY_MS) {
    connect_now_(now_ms);
  }
}

bool WifiLink::is_connected() const {
  return WiFi.status() == WL_CONNECTED;
}
