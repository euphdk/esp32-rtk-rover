#include "gnss_uart.h"

#include "log.h"

bool GnssUart::begin(int uart_num, uint32_t baud, int rx_pin, int tx_pin) {
  serial_ = new HardwareSerial(uart_num);
  serial_->begin(baud, SERIAL_8N1, rx_pin, tx_pin);
  LOGI("GNSS UART started: uart=%d baud=%lu rx=%d tx=%d", uart_num,
       static_cast<unsigned long>(baud), rx_pin, tx_pin);
  return true;
}

int GnssUart::available() {
  if (serial_ == nullptr) {
    return 0;
  }
  return serial_->available();
}

int GnssUart::read() {
  if (serial_ == nullptr) {
    return -1;
  }
  return serial_->read();
}

size_t GnssUart::write_bytes(const uint8_t* data, size_t len) {
  if (serial_ == nullptr || data == nullptr || len == 0) {
    return 0;
  }
  return serial_->write(data, len);
}

size_t GnssUart::write_line(const char* line) {
  if (serial_ == nullptr || line == nullptr) {
    return 0;
  }
  return serial_->print(line);
}
