#include "mdns_link.h"

#include <ESPmDNS.h>

#include "log.h"

void MdnsLink::begin(const char* hostname, const char* instance_name,
                     unsigned short port) {
  hostname_ = hostname;
  instance_name_ = instance_name;
  port_ = port;
}

bool MdnsLink::start_() {
  if (hostname_ == nullptr || hostname_[0] == '\0') {
    LOGW("mDNS disabled: empty hostname");
    return false;
  }

  if (!MDNS.begin(hostname_)) {
    LOGW("mDNS begin failed for %s.local", hostname_);
    return false;
  }

  MDNS.setInstanceName(instance_name_ != nullptr ? instance_name_ : hostname_);
  MDNS.addService("nmea-0183", "tcp", port_);
  MDNS.addServiceTxt("nmea-0183", "tcp", "proto", "tcp");
  MDNS.addServiceTxt("nmea-0183", "tcp", "role", "rover");
  MDNS.addServiceTxt("nmea-0183", "tcp", "ver", "0.1");

  LOGI("mDNS started: host=%s.local service=_nmea-0183._tcp port=%u", hostname_,
       static_cast<unsigned>(port_));
  return true;
}

void MdnsLink::stop_() {
  if (!started_) {
    return;
  }
  MDNS.end();
  started_ = false;
  LOGI("mDNS stopped");
}

void MdnsLink::tick(bool wifi_connected) {
  if (!wifi_connected) {
    if (started_) {
      stop_();
    }
    prev_wifi_connected_ = false;
    return;
  }

  if (!started_ || !prev_wifi_connected_) {
    started_ = start_();
  }

  prev_wifi_connected_ = true;
}

bool MdnsLink::is_started() const { return started_; }
