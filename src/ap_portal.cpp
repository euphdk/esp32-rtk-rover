#include "ap_portal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "config_store.h"
#include "log.h"

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

static void handle_root() {
  String ssid = (g_cfg != nullptr) ? String(g_cfg->wifi_ssid) : String("");
  String host = (g_cfg != nullptr) ? String(g_cfg->ntrip_host) : String("");
  String mount = (g_cfg != nullptr) ? String(g_cfg->ntrip_mountpoint) : String("");

  String page;
  page.reserve(2200);
  page += "<!doctype html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>Rover Setup</title>";
  page += "<style>body{font-family:Arial,sans-serif;margin:16px;max-width:560px;}";
  page += "label{display:block;margin-top:12px;font-weight:600;}";
  page += "input{width:100%;padding:10px;margin-top:4px;box-sizing:border-box;}";
  page += "button{margin-top:16px;padding:10px 14px;}small{color:#666;}</style></head><body>";
  page += "<h2>ESP32 RTK Rover Setup</h2>";
  page += "<p>Configure Wi-Fi. NTRIP fields are shown for upcoming support.</p>";
  page += "<form method='POST' action='/wifi'>";
  page += "<label>Wi-Fi SSID</label><input name='ssid' maxlength='32' value='";
  page += html_escape(ssid);
  page += "' required>";
  page += "<label>Wi-Fi Password</label><input name='pass' maxlength='64' type='password' value=''>";
  page += "<small>Leave blank to keep current saved password.</small>";
  page += "<h3 style='margin-top:20px'>NTRIP (next step)</h3>";
  page += "<label>NTRIP Host</label><input value='";
  page += html_escape(host);
  page += "' disabled>";
  page += "<label>Mountpoint</label><input value='";
  page += html_escape(mount);
  page += "' disabled>";
  page += "<button type='submit'>Save Wi-Fi and Reboot</button></form>";
  page += "<p><small>AP IP: 192.168.4.1</small></p></body></html>";

  g_server.send(200, "text/html", page);
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
  out += "\"}";
  g_server.send(200, "application/json", out);
}

static void handle_wifi_save() {
  String ssid = g_server.arg("ssid");
  String pass = g_server.arg("pass");

  ssid.trim();
  pass.trim();

  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64) {
    g_server.send(400, "text/plain", "Invalid SSID/password length");
    return;
  }

  String final_pass = pass;
  if (final_pass.length() == 0 && g_cfg != nullptr) {
    final_pass = String(g_cfg->wifi_pass);
  }

  if (!config_store_save_wifi(g_cfg, ssid.c_str(), final_pass.c_str())) {
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
  g_server.on("/wifi", HTTP_POST, handle_wifi_save);
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
    wifi_down_since_ms_ = 0;
    if (ap_active_) {
      stop_ap_();
      WiFi.mode(WIFI_STA);
    }
    prev_wifi_connected_ = true;
    return;
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
