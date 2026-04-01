#include "status.h"

#include <string.h>

RoverStatus g_status;

void status_init() {
  memset(&g_status, 0, sizeof(g_status));
  g_status.gnss_hdop_tenths = -1;
  strncpy(g_status.last_error, "none", sizeof(g_status.last_error) - 1);
}

void status_set_error(const char* msg) {
  if (msg == nullptr || msg[0] == '\0') {
    return;
  }
  strncpy(g_status.last_error, msg, sizeof(g_status.last_error) - 1);
  g_status.last_error[sizeof(g_status.last_error) - 1] = '\0';
}
