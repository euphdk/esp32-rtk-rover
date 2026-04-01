#pragma once

#include <stdint.h>
#include <WiFi.h>

class NmeaServer {
 public:
  explicit NmeaServer(uint16_t port) : server_(port) {}

  void begin();
  void tick();
  bool has_client();
  void send_line(const char* line);

 private:
  WiFiServer server_;
  WiFiClient client_;
};
