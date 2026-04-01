#include "ap_portal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "config_store.h"
#include "log.h"
#include "status.h"

static WebServer g_server(WIFI_AP_PORTAL_PORT);
static RoverConfig* g_cfg = nullptr;

static String html_escape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static String masked_secret(const char* raw) {
  if (raw == nullptr || raw[0] == '\0') {
    return "(empty)";
  }
  return "********";
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

static String health_line() {
  const uint32_t now_ms = millis();
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

  char buf[320];
  snprintf(
      buf, sizeof(buf),
      "st wifi=%d ap=%d cfg=%s mdns=%d ntrip=%d qf=%d rtcm=%lu rx=%lu nmea_in=%lu out=%lu bad=%lu "
      "fix=%s sat=%u hdop=%d.%d age_nmea=%lus age_rtcm=%lus err=%s",
      static_cast<int>(g_status.wifi_connected), static_cast<int>(g_status.ap_active),
      g_status.cfg_from_nvs ? "nvs" : "default", static_cast<int>(g_status.mdns_started),
      static_cast<int>(g_status.ntrip_connected), static_cast<int>(g_status.qfield_client_connected),
      static_cast<unsigned long>(g_status.rtcm_bytes_in),
      static_cast<unsigned long>(g_status.gnss_rx_bytes),
      static_cast<unsigned long>(g_status.nmea_lines_in),
      static_cast<unsigned long>(g_status.nmea_lines_out),
      static_cast<unsigned long>(g_status.nmea_bad_checksum),
      fix_quality_text(g_status.gnss_fix_quality), static_cast<unsigned>(g_status.gnss_sats_used),
      hdop_whole, hdop_frac, static_cast<unsigned long>(nmea_age_s),
      static_cast<unsigned long>(rtcm_age_s), g_status.last_error);
  return String(buf);
}

static void handle_root() {
  String ssid = (g_cfg != nullptr) ? String(g_cfg->wifi_ssid) : String("");
  String wifi_pass_mask = (g_cfg != nullptr) ? masked_secret(g_cfg->wifi_pass) : String("(empty)");
  String user = (g_cfg != nullptr) ? String(g_cfg->ntrip_user) : String("");
  String ntrip_pass_mask = (g_cfg != nullptr) ? masked_secret(g_cfg->ntrip_pass) : String("(empty)");
  String host = (g_cfg != nullptr) ? String(g_cfg->ntrip_host) : String("");
  String port = (g_cfg != nullptr) ? String(g_cfg->ntrip_port) : String("");
  String mount = (g_cfg != nullptr) ? String(g_cfg->ntrip_mountpoint) : String("");
  String stat = health_line();

  String page;
  page.reserve(4200);
  page += "<!doctype html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Rover Setup</title>";
  page += "<style>body{font-family:Arial,sans-serif;margin:16px;max-width:560px;}";
  page += "label{display:block;margin-top:12px;font-weight:600;}";
  page += "input{width:100%;padding:10px;margin-top:4px;box-sizing:border-box;}";
  page += "button{margin-top:16px;padding:10px 14px;}small{color:#666;}</style></head><body>";
  page += "<h2>ESP32 RTK Rover Setup</h2>";
  page += "<p>Configure Wi-Fi and NTRIP caster settings.</p>";
  page += "<h3>Current Config</h3>";
  page += "<p><b>Wi-Fi SSID:</b> ";
  page += html_escape(ssid);
  page += "<br><b>Wi-Fi Password:</b> ";
  page += html_escape(wifi_pass_mask);
  page += "<br><b>NTRIP Host:</b> ";
  page += html_escape(host);
  page += "<br><b>NTRIP Port:</b> ";
  page += html_escape(port);
  page += "<br><b>NTRIP Mount:</b> ";
  page += html_escape(mount);
  page += "<br><b>NTRIP User:</b> ";
  page += html_escape(user);
  page += "<br><b>NTRIP Password:</b> ";
  page += html_escape(ntrip_pass_mask);
  page += "</p>";

  page += "<h3>Live Status</h3><pre style='white-space:pre-wrap;background:#f4f4f4;padding:10px;border-radius:4px'>";
  page += html_escape(stat);
  page += "</pre>";

  page += "<form method='POST' action='/config'>";
  page += "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='";
  page += html_escape(ssid);
  page += "' required>";
  page += "<label>Wi-Fi Password</label><input name='pass' maxlength='64' type='password' value=''>";
  page += "<small>Leave blank to keep current saved password.</small>";
  page += "<h3 style='margin-top:20px'>NTRIP</h3>";
  page += "<label>NTRIP Host</label><input name='ntrip_host' maxlength='63' value='";
  page += html_escape(host);
  page += "' required>";
  page += "<label>NTRIP Port</label><input name='ntrip_port' type='number' min='1' max='65535' value='";
  page += html_escape(port);
  page += "' required>";
  page += "<label>Mountpoint</label><input name='ntrip_mount' maxlength='63' value='";
  page += html_escape(mount);
  page += "' required>";
  page += "<label>NTRIP User</label><input name='ntrip_user' maxlength='63' value='";
  page += html_escape(user);
  page += "'>";
  page += "<label>NTRIP Password</label><input name='ntrip_pass' maxlength='63' type='password' value=''>";
  page += "<small>Leave blank to keep current saved NTRIP password.</small>";
  page += "<button type='submit'>Save Config and Reboot</button></form>";
  page += "<h3 style='margin-top:20px'>Danger Zone</h3>";
  page += "<form method='POST' action='/delete_wifi' onsubmit='return confirm(\"Delete saved Wi-Fi and reboot?\");'>";
  page += "<button type='submit'>Delete Saved Wi-Fi</button></form>";
  page += "<form method='POST' action='/delete_ntrip' onsubmit='return confirm(\"Delete saved NTRIP and reboot?\");'>";
  page += "<button type='submit'>Delete Saved NTRIP</button></form>";
  page += "<p><small>AP IP: 192.168.4.1</small></p></body></html>";

  g_server.send(200, "text/html", page);
}

static void handle_redirect_root() {
  g_server.sendHeader("Location", "/", true);
  g_server.send(302, "text/plain", "");
}

static void handle_status() {
  String out;
  out.reserve(256);
  out += "{";
  out += "\"sta_connected\":";
  out += (WiFi.status() == WL_CONNECTED) ? "true" : "false";
  out += ",\"sta_ip\":\"";
  out += WiFi.localIP().toString();
  out += "\",\"ap_ip\":\"";
  out += WiFi.softAPIP().toString();
  out += "\",\"health\":\"";
  out += health_line();
  out += "\"}";
  g_server.send(200, "application/json", out);
}

static void handle_delete_wifi() {
  if (g_cfg == nullptr) {
    g_server.send(500, "text/plain", "Config not initialized");
    return;
  }

  g_cfg->wifi_ssid[0] = '\0';
  g_cfg->wifi_pass[0] = '\0';
  if (!config_store_save_all(g_cfg)) {
    g_server.send(500, "text/plain", "Failed to save config");
    return;
  }

  g_server.send(200, "text/html", "<html><body><h3>Wi-Fi deleted. Rebooting...</h3></body></html>");
  delay(600);
  ESP.restart();
}

static void handle_delete_ntrip() {
  if (g_cfg == nullptr) {
    g_server.send(500, "text/plain", "Config not initialized");
    return;
  }

  g_cfg->ntrip_host[0] = '\0';
  g_cfg->ntrip_mountpoint[0] = '\0';
  g_cfg->ntrip_user[0] = '\0';
  g_cfg->ntrip_pass[0] = '\0';
  g_cfg->ntrip_port = NTRIP_PORT;

  if (!config_store_save_all(g_cfg)) {
    g_server.send(500, "text/plain", "Failed to save config");
    return;
  }

  g_server.send(200, "text/html", "<html><body><h3>NTRIP deleted. Rebooting...</h3></body></html>");
  delay(600);
  ESP.restart();
}

static void handle_config_save() {
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");
  String ntrip_host = g_server.arg("ntrip_host");
  String ntrip_port = g_server.arg("ntrip_port");
  String ntrip_mount = g_server.arg("ntrip_mount");
  String ntrip_user = g_server.arg("ntrip_user");
  String ntrip_pass = g_server.arg("ntrip_pass");

  ssid.trim();
  pass.trim();
  ntrip_host.trim();
  ntrip_port.trim();
  ntrip_mount.trim();
  ntrip_user.trim();
  ntrip_pass.trim();

  const long parsed_port = ntrip_port.toInt();
  const bool ntrip_disabled = (ntrip_host.length() == 0 && ntrip_mount.length() == 0);
  const bool ntrip_partial = (ntrip_host.length() == 0) != (ntrip_mount.length() == 0);

  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64 ||
      ntrip_host.length() > 63 || ntrip_mount.length() > 63 ||
      ntrip_user.length() > 63 || ntrip_pass.length() > 63 ||
      parsed_port <= 0 || parsed_port > 65535 || ntrip_partial) {
    g_server.send(400, "text/plain", "Invalid config values");
    return;
  }

  String final_pass = pass;
  if (final_pass.length() == 0 && g_cfg != nullptr) {
    final_pass = String(g_cfg->wifi_pass);
  }

  String final_ntrip_pass = ntrip_pass;
  if (final_ntrip_pass.length() == 0 && g_cfg != nullptr) {
    final_ntrip_pass = String(g_cfg->ntrip_pass);
  }

  if (g_cfg == nullptr) {
    g_server.send(500, "text/plain", "Config not initialized");
    return;
  }

  strncpy(g_cfg->wifi_ssid, ssid.c_str(), sizeof(g_cfg->wifi_ssid) - 1);
  g_cfg->wifi_ssid[sizeof(g_cfg->wifi_ssid) - 1] = '\0';
  strncpy(g_cfg->wifi_pass, final_pass.c_str(), sizeof(g_cfg->wifi_pass) - 1);
  g_cfg->wifi_pass[sizeof(g_cfg->wifi_pass) - 1] = '\0';

  strncpy(g_cfg->ntrip_host, ntrip_host.c_str(), sizeof(g_cfg->ntrip_host) - 1);
  g_cfg->ntrip_host[sizeof(g_cfg->ntrip_host) - 1] = '\0';
  g_cfg->ntrip_port = ntrip_disabled ? NTRIP_PORT : static_cast<uint16_t>(parsed_port);
  strncpy(g_cfg->ntrip_mountpoint, ntrip_mount.c_str(), sizeof(g_cfg->ntrip_mountpoint) - 1);
  g_cfg->ntrip_mountpoint[sizeof(g_cfg->ntrip_mountpoint) - 1] = '\0';
  strncpy(g_cfg->ntrip_user, ntrip_user.c_str(), sizeof(g_cfg->ntrip_user) - 1);
  g_cfg->ntrip_user[sizeof(g_cfg->ntrip_user) - 1] = '\0';
  strncpy(g_cfg->ntrip_pass, final_ntrip_pass.c_str(), sizeof(g_cfg->ntrip_pass) - 1);
  g_cfg->ntrip_pass[sizeof(g_cfg->ntrip_pass) - 1] = '\0';

  if (!config_store_save_all(g_cfg)) {
    g_server.send(500, "text/plain", "Failed to save config");
    return;
  }

  g_server.send(200, "text/html",
                "<html><body><h3>Saved. Rebooting...</h3></body></html>");
  delay(600);
  ESP.restart();
}

void ApPortal::begin(RoverConfig* cfg) {
  cfg_ = cfg;
  wifi_down_since_ms_ = 0;
  prev_wifi_connected_ = false;
}

void ApPortal::start_server_() {
  if (server_active_) {
    return;
  }

  g_cfg = cfg_;
  g_server.on("/", HTTP_GET, handle_root);
  g_server.on("/status", HTTP_GET, handle_status);
  g_server.on("/config", HTTP_POST, handle_config_save);
  g_server.on("/wifi", HTTP_POST, handle_config_save);
  g_server.on("/delete_wifi", HTTP_POST, handle_delete_wifi);
  g_server.on("/delete_ntrip", HTTP_POST, handle_delete_ntrip);
  g_server.on("/generate_204", HTTP_GET, handle_redirect_root);      // Android captive probe
  g_server.on("/hotspot-detect.html", HTTP_GET, handle_redirect_root);  // Apple captive probe
  g_server.on("/ncsi.txt", HTTP_GET, handle_redirect_root);          // Windows captive probe
  g_server.onNotFound(handle_redirect_root);
  g_server.begin();
  server_active_ = true;
  LOGI("Portal server started: http://192.168.4.1:%u",
       static_cast<unsigned>(WIFI_AP_PORTAL_PORT));
}

void ApPortal::stop_server_() {
  if (!server_active_) {
    return;
  }
  g_server.stop();
  server_active_ = false;
  LOGI("Portal server stopped");
}

void ApPortal::start_ap_() {
  if (ap_active_) {
    return;
  }

  uint64_t mac = ESP.getEfuseMac();
  char ssid[40];
  snprintf(ssid, sizeof(ssid), "%s-%02X%02X%02X", WIFI_AP_SSID_PREFIX,
           static_cast<unsigned>((mac >> 16) & 0xFF),
           static_cast<unsigned>((mac >> 8) & 0xFF),
           static_cast<unsigned>(mac & 0xFF));

  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(ssid, WIFI_AP_PASS)) {
    LOGW("AP start failed");
    return;
  }

  ap_active_ = true;
  LOGI("AP started: ssid=%s ip=%s", ssid, WiFi.softAPIP().toString().c_str());
  start_server_();
}

void ApPortal::stop_ap_() {
  if (!ap_active_) {
    return;
  }

  stop_server_();
  WiFi.softAPdisconnect(true);
  ap_active_ = false;
  LOGI("AP stopped");
}

void ApPortal::tick(uint32_t now_ms, bool wifi_connected) {
  if (!WIFI_AP_CONFIG_ENABLE) {
    return;
  }

  if (wifi_connected) {
    if (!server_active_) {
      start_server_();
    }
    wifi_down_since_ms_ = 0;
    if (ap_active_) {
      stop_ap_();
      WiFi.mode(WIFI_STA);
      if (!server_active_) {
        start_server_();
      }
    }
    prev_wifi_connected_ = true;
  }

  const bool wifi_config_missing =
      (cfg_ == nullptr || cfg_->wifi_ssid[0] == '\0');
  if (wifi_config_missing && !ap_active_) {
    LOGW("Wi-Fi not configured, starting AP portal now");
    start_ap_();
  }

  if (prev_wifi_connected_) {
    wifi_down_since_ms_ = now_ms;
    prev_wifi_connected_ = false;
  }

  if (wifi_down_since_ms_ == 0) {
    wifi_down_since_ms_ = now_ms;
  }

  if (!ap_active_ && (now_ms - wifi_down_since_ms_) >= WIFI_AP_FALLBACK_MS) {
    start_ap_();
  }

  if (server_active_) {
    g_server.handleClient();
  }
}

bool ApPortal::ap_active() const { return ap_active_; }
