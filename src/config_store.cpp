#include "config_store.h"

#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "log.h"

static const char* kNs = "rover";
static const char* kKeyMarker = "cfg_v1";
static const uint8_t kMarkerVal = 1;

static void copy_str(char* dst, size_t dst_len, const char* src) {
  if (dst == nullptr || dst_len == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = '\0';
}

static void set_defaults(RoverConfig* cfg) {
  memset(cfg, 0, sizeof(*cfg));
  copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), WIFI_SSID);
  copy_str(cfg->wifi_pass, sizeof(cfg->wifi_pass), WIFI_PASS);

  copy_str(cfg->ntrip_host, sizeof(cfg->ntrip_host), NTRIP_HOST);
  cfg->ntrip_port = NTRIP_PORT;
  copy_str(cfg->ntrip_mountpoint, sizeof(cfg->ntrip_mountpoint), NTRIP_MOUNTPOINT);
  copy_str(cfg->ntrip_user, sizeof(cfg->ntrip_user), NTRIP_USER);
  copy_str(cfg->ntrip_pass, sizeof(cfg->ntrip_pass), NTRIP_PASS);
}

bool config_store_save_all(RoverConfig* cfg) {
  if (cfg == nullptr || cfg->wifi_ssid[0] == '\0') {
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(kNs, false)) {
    LOGW("cfg: NVS write open failed");
    return false;
  }

  bool ok = true;
  ok = ok && prefs.putUChar(kKeyMarker, kMarkerVal) == 1;
  ok = ok && prefs.putString("wifi_ssid", cfg->wifi_ssid) > 0;
  ok = ok && prefs.putString("wifi_pass", cfg->wifi_pass) >= 0;

  ok = ok && prefs.putString("ntrip_host", cfg->ntrip_host) >= 0;
  ok = ok && prefs.putUShort("ntrip_port", cfg->ntrip_port) > 0;
  ok = ok && prefs.putString("ntrip_mount", cfg->ntrip_mountpoint) >= 0;
  ok = ok && prefs.putString("ntrip_user", cfg->ntrip_user) >= 0;
  ok = ok && prefs.putString("ntrip_pass", cfg->ntrip_pass) >= 0;

  prefs.end();

  if (!ok) {
    LOGW("cfg: save failed");
    return false;
  }

  LOGI("cfg: full config saved to NVS");
  return true;
}

void config_store_load(RoverConfig* cfg, bool* loaded_from_nvs) {
  if (cfg == nullptr) {
    return;
  }

  if (loaded_from_nvs != nullptr) {
    *loaded_from_nvs = false;
  }

  set_defaults(cfg);

  Preferences prefs;
  if (!prefs.begin(kNs, true)) {
    LOGW("cfg: NVS open failed, using defaults");
    return;
  }

  const uint8_t marker = prefs.getUChar(kKeyMarker, 0);
  if (marker != kMarkerVal) {
    prefs.end();
    LOGI("cfg: no saved config, using defaults");
    return;
  }

  copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid),
           prefs.getString("wifi_ssid", cfg->wifi_ssid).c_str());
  copy_str(cfg->wifi_pass, sizeof(cfg->wifi_pass),
           prefs.getString("wifi_pass", cfg->wifi_pass).c_str());

  // NTRIP values are loaded for future use.
  copy_str(cfg->ntrip_host, sizeof(cfg->ntrip_host),
           prefs.getString("ntrip_host", cfg->ntrip_host).c_str());
  cfg->ntrip_port = prefs.getUShort("ntrip_port", cfg->ntrip_port);
  copy_str(cfg->ntrip_mountpoint, sizeof(cfg->ntrip_mountpoint),
           prefs.getString("ntrip_mount", cfg->ntrip_mountpoint).c_str());
  copy_str(cfg->ntrip_user, sizeof(cfg->ntrip_user),
           prefs.getString("ntrip_user", cfg->ntrip_user).c_str());
  copy_str(cfg->ntrip_pass, sizeof(cfg->ntrip_pass),
           prefs.getString("ntrip_pass", cfg->ntrip_pass).c_str());

  prefs.end();

  if (loaded_from_nvs != nullptr) {
    *loaded_from_nvs = true;
  }
  LOGI("cfg: loaded from NVS");
}

bool config_store_save_wifi(RoverConfig* cfg, const char* ssid, const char* pass) {
  if (cfg == nullptr || ssid == nullptr || ssid[0] == '\0' || pass == nullptr) {
    return false;
  }

  copy_str(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), ssid);
  copy_str(cfg->wifi_pass, sizeof(cfg->wifi_pass), pass);
  return config_store_save_all(cfg);
}
