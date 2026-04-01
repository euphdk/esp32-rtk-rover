#pragma once

class MdnsLink {
 public:
  void begin(const char* hostname, const char* instance_name, unsigned short port);
  void tick(bool wifi_connected);
  bool is_started() const;

 private:
  bool start_();
  void stop_();

  const char* hostname_ = nullptr;
  const char* instance_name_ = nullptr;
  unsigned short port_ = 0;
  bool started_ = false;
  bool prev_wifi_connected_ = false;
};
