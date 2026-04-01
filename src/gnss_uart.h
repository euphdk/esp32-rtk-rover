#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class GnssUart {
 public:
  bool begin(int uart_num, uint32_t baud, int rx_pin, int tx_pin);
  int available();
  int read();
  size_t write_bytes(const uint8_t* data, size_t len);
  size_t write_line(const char* line);

 private:
  HardwareSerial* serial_ = nullptr;
};
