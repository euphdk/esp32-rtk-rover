#include "nmea_server.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "status.h"

void NmeaServer::begin() {
  server_.begin();
  server_.setNoDelay(true);
  LOGI("NMEA TCP server started on port %u", NMEA_TCP_PORT);
}

void NmeaServer::tick() {
  if (client_ && !client_.connected()) {
    client_.stop();
  }

  WiFiClient incoming = server_.available();
  if (!incoming) {
    return;
  }

  if (ALLOW_ONLY_ONE_TCP_CLIENT && client_ && client_.connected()) {
    LOGW("Rejecting extra TCP client %s", incoming.remoteIP().toString().c_str());
    incoming.stop();
    return;
  }

  if (client_ && client_.connected()) {
    client_.stop();
  }

  client_ = incoming;
  client_.setNoDelay(true);
  LOGI("QField TCP client connected: %s", client_.remoteIP().toString().c_str());
}

bool NmeaServer::has_client() {
  return client_ && client_.connected();
}

void NmeaServer::send_line(const char* line) {
  if (line == nullptr || !has_client()) {
    return;
  }

  size_t n = client_.write(reinterpret_cast<const uint8_t*>(line), strlen(line));
  if (n > 0) {
    g_status.nmea_lines_out++;
    g_status.last_qfield_tx_ms = millis();
  }
}
