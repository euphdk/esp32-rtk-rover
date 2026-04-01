#include "ap_portal.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "config_store.h"
#include "log.h"
#include "status.h"

static WebServer g_server(WIFI_AP_PORTAL_PORT);
static RoverConfig* g_cfg = nullptr;

static const char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static String base64_encode(const char* in) {
  String out;
  if (in == nullptr) {
    return out;
  }
  size_t len = strlen(in);
  out.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t octet_a = static_cast<uint8_t>(in[i]);
    uint32_t octet_b = (i + 1 < len) ? static_cast<uint8_t>(in[i + 1]) : 0;
    uint32_t octet_c = (i + 2 < len) ? static_cast<uint8_t>(in[i + 2]) : 0;
    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    out += kB64Table[(triple >> 18) & 0x3F];
    out += kB64Table[(triple >> 12) & 0x3F];
    out += (i + 1 < len) ? kB64Table[(triple >> 6) & 0x3F] : '=';
    out += (i + 2 < len) ? kB64Table[triple & 0x3F] : '=';
  }
  return out;
}

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

static String json_escape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
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
      return "No fix";
    case 1:
      return "SPS";
    case 2:
      return "DGPS";
    case 4:
      return "RTK fixed";
    case 5:
      return "RTK float";
    case 6:
      return "Dead reckoning";
    default:
      return "Other";
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

  char buf[360];
  snprintf(
      buf, sizeof(buf),
      "st wifi=%d ap=%d cfg=%s mdns=%d ntrip=%d qf=%d rtcm=%lu rx=%lu nmea_in=%lu out=%lu bad=%lu tlong=%lu "
      "fix=%s sat=%u hdop=%d.%d age_nmea=%lus age_rtcm=%lus err=%s",
      static_cast<int>(g_status.wifi_connected), static_cast<int>(g_status.ap_active),
      g_status.cfg_from_nvs ? "nvs" : "default", static_cast<int>(g_status.mdns_started),
      static_cast<int>(g_status.ntrip_connected), static_cast<int>(g_status.qfield_client_connected),
      static_cast<unsigned long>(g_status.rtcm_bytes_in),
      static_cast<unsigned long>(g_status.gnss_rx_bytes),
      static_cast<unsigned long>(g_status.nmea_lines_in),
      static_cast<unsigned long>(g_status.nmea_lines_out),
      static_cast<unsigned long>(g_status.nmea_bad_checksum),
      static_cast<unsigned long>(g_status.nmea_too_long), fix_quality_text(g_status.gnss_fix_quality),
      static_cast<unsigned>(g_status.gnss_sats_used), hdop_whole, hdop_frac,
      static_cast<unsigned long>(nmea_age_s), static_cast<unsigned long>(rtcm_age_s),
      g_status.last_error);
  return String(buf);
}

static String common_head(const char* title) {
  String page;
  page.reserve(1000);
  page += "<!doctype html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  page += "<title>";
  page += title;
  page += "</title>";
  page += "<style>";
  page += "body{font-family:Arial,sans-serif;margin:16px;max-width:900px;color:#1f2937;}";
  page += "nav a{margin-right:14px;text-decoration:none;color:#0b66c3;font-weight:700;}";
  page += "h2{margin-bottom:6px;}h3{margin:18px 0 10px;}";
  page += "label{display:block;margin-top:12px;font-weight:600;}";
  page += "input{width:100%;padding:10px;margin-top:4px;box-sizing:border-box;}";
  page += "button{margin-top:12px;padding:10px 14px;}";
  page += ".muted{color:#666;font-size:0.92em;}";
  page += ".card{background:#f7f9fb;border:1px solid #dbe3ea;border-radius:8px;padding:12px;margin:10px 0;}";
  page += "table{width:100%;border-collapse:collapse;}th,td{padding:8px;border-bottom:1px solid #e6ebf0;text-align:left;}";
  page += "pre{white-space:pre-wrap;background:#f4f4f4;padding:10px;border-radius:6px;}";
  page += "</style></head><body>";
  page += "<h2>ESP32 RTK Rover</h2>";
  page += "<nav><a href='/status'>Status</a><a href='/config'>Config</a></nav>";
  return page;
}

static void handle_redirect_status() {
  g_server.sendHeader("Location", "/status", true);
  g_server.send(302, "text/plain", "");
}

static void handle_api_status() {
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

  String out;
  out.reserve(900);
  out += "{";
  out += "\"wifi_connected\":";
  out += g_status.wifi_connected ? "true" : "false";
  out += ",\"ap_active\":";
  out += g_status.ap_active ? "true" : "false";
  out += ",\"mdns_started\":";
  out += g_status.mdns_started ? "true" : "false";
  out += ",\"cfg_source\":\"";
  out += g_status.cfg_from_nvs ? "nvs" : "default";
  out += "\",\"ntrip_connected\":";
  out += g_status.ntrip_connected ? "true" : "false";
  out += ",\"qfield_connected\":";
  out += g_status.qfield_client_connected ? "true" : "false";
  out += ",\"sta_ip\":\"";
  out += WiFi.localIP().toString();
  out += "\",\"ap_ip\":\"";
  out += WiFi.softAPIP().toString();
  out += "\",\"rssi\":";
  out += String(WiFi.RSSI());
  out += ",\"rtcm_bytes\":";
  out += String(g_status.rtcm_bytes_in);
  out += ",\"gnss_rx_bytes\":";
  out += String(g_status.gnss_rx_bytes);
  out += ",\"nmea_in\":";
  out += String(g_status.nmea_lines_in);
  out += ",\"nmea_out\":";
  out += String(g_status.nmea_lines_out);
  out += ",\"nmea_bad_checksum\":";
  out += String(g_status.nmea_bad_checksum);
  out += ",\"nmea_too_long\":";
  out += String(g_status.nmea_too_long);
  out += ",\"fix_quality\":\"";
  out += fix_quality_text(g_status.gnss_fix_quality);
  out += "\",\"satellites\":";
  out += String(g_status.gnss_sats_used);
  out += ",\"hdop\":\"";
  out += String(hdop_whole);
  out += ".";
  out += String(hdop_frac);
  out += "\",\"nmea_age_s\":";
  out += String(nmea_age_s);
  out += ",\"rtcm_age_s\":";
  out += String(rtcm_age_s);
  out += ",\"last_error\":\"";
  out += json_escape(String(g_status.last_error));
  out += "\",\"health_line\":\"";
  out += json_escape(health_line());
  out += "\"}";

  g_server.send(200, "application/json", out);
}

static void handle_status_page() {
  String page = common_head("Rover Status");
  page.reserve(5000);
  page += "<p class='muted'>Auto-updates every 2 seconds.</p>";

  page += "<div class='card'><b>Connectivity</b><table>";
  page += "<tr><th>Wi-Fi STA</th><td id='wifi_connected'>-</td></tr>";
  page += "<tr><th>AP Portal</th><td id='ap_active'>-</td></tr>";
  page += "<tr><th>mDNS</th><td id='mdns_started'>-</td></tr>";
  page += "<tr><th>Config Source</th><td id='cfg_source'>-</td></tr>";
  page += "<tr><th>STA IP</th><td id='sta_ip'>-</td></tr>";
  page += "<tr><th>AP IP</th><td id='ap_ip'>-</td></tr>";
  page += "<tr><th>RSSI (dBm)</th><td id='rssi'>-</td></tr></table></div>";

  page += "<div class='card'><b>RTK / Client</b><table>";
  page += "<tr><th>NTRIP Stream</th><td id='ntrip_connected'>-</td></tr>";
  page += "<tr><th>QField TCP Client</th><td id='qfield_connected'>-</td></tr>";
  page += "<tr><th>RTCM Bytes</th><td id='rtcm_bytes'>-</td></tr>";
  page += "<tr><th>RTCM Data Age</th><td id='rtcm_age_s'>-</td></tr></table></div>";

  page += "<div class='card'><b>GNSS / NMEA</b><table>";
  page += "<tr><th>Fix Quality</th><td id='fix_quality'>-</td></tr>";
  page += "<tr><th>Satellites Used</th><td id='satellites'>-</td></tr>";
  page += "<tr><th>HDOP</th><td id='hdop'>-</td></tr>";
  page += "<tr><th>GNSS RX Bytes</th><td id='gnss_rx_bytes'>-</td></tr>";
  page += "<tr><th>NMEA In</th><td id='nmea_in'>-</td></tr>";
  page += "<tr><th>NMEA Out</th><td id='nmea_out'>-</td></tr>";
  page += "<tr><th>NMEA Bad Checksum</th><td id='nmea_bad_checksum'>-</td></tr>";
  page += "<tr><th>NMEA Too Long</th><td id='nmea_too_long'>-</td></tr>";
  page += "<tr><th>NMEA Data Age</th><td id='nmea_age_s'>-</td></tr></table></div>";

  page += "<div class='card'><b>Errors</b><table>";
  page += "<tr><th>Last Error</th><td id='last_error'>-</td></tr></table>";
  page += "<pre id='health_line'>-</pre></div>";

  page += "<script>";
  page += "function setText(id,val){const el=document.getElementById(id);if(el)el.textContent=val;}";
  page += "function yesNo(v){return v?'Connected':'Not connected';}";
  page += "async function refreshStatus(){";
  page += "try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)return;const s=await r.json();";
  page += "setText('wifi_connected',yesNo(s.wifi_connected));";
  page += "setText('ap_active',s.ap_active?'Active':'Off');";
  page += "setText('mdns_started',s.mdns_started?'Started':'Stopped');";
  page += "setText('cfg_source',s.cfg_source);";
  page += "setText('sta_ip',s.sta_ip);setText('ap_ip',s.ap_ip);setText('rssi',s.rssi);";
  page += "setText('ntrip_connected',yesNo(s.ntrip_connected));";
  page += "setText('qfield_connected',s.qfield_connected?'Connected':'Disconnected');";
  page += "setText('rtcm_bytes',s.rtcm_bytes);setText('rtcm_age_s',s.rtcm_age_s+' s');";
  page += "setText('fix_quality',s.fix_quality);setText('satellites',s.satellites);setText('hdop',s.hdop);";
  page += "setText('gnss_rx_bytes',s.gnss_rx_bytes);setText('nmea_in',s.nmea_in);setText('nmea_out',s.nmea_out);";
  page += "setText('nmea_bad_checksum',s.nmea_bad_checksum);setText('nmea_too_long',s.nmea_too_long);";
  page += "setText('nmea_age_s',s.nmea_age_s+' s');setText('last_error',s.last_error);setText('health_line',s.health_line);";
  page += "}catch(e){setText('last_error','status fetch failed');}}";
  page += "refreshStatus();setInterval(refreshStatus,2000);";
  page += "</script></body></html>";

  g_server.send(200, "text/html", page);
}

static void handle_config_page() {
  String ssid = (g_cfg != nullptr) ? String(g_cfg->wifi_ssid) : String("");
  String wifi_pass_mask = (g_cfg != nullptr) ? masked_secret(g_cfg->wifi_pass) : String("(empty)");
  String user = (g_cfg != nullptr) ? String(g_cfg->ntrip_user) : String("");
  String ntrip_pass_mask = (g_cfg != nullptr) ? masked_secret(g_cfg->ntrip_pass) : String("(empty)");
  String host = (g_cfg != nullptr) ? String(g_cfg->ntrip_host) : String("");
  String port = (g_cfg != nullptr) ? String(g_cfg->ntrip_port) : String("");
  String mount = (g_cfg != nullptr) ? String(g_cfg->ntrip_mountpoint) : String("");

  String page = common_head("Rover Config");
  page.reserve(5200);
  page += "<p class='muted'>Edit settings below. Saving triggers reboot.</p>";

  page += "<div class='card'><h3>Current Saved Values</h3>";
  page += "<table>";
  page += "<tr><th>Wi-Fi SSID</th><td>" + html_escape(ssid) + "</td></tr>";
  page += "<tr><th>Wi-Fi Password</th><td>" + html_escape(wifi_pass_mask) + "</td></tr>";
  page += "<tr><th>NTRIP Host</th><td>" + html_escape(host) + "</td></tr>";
  page += "<tr><th>NTRIP Port</th><td>" + html_escape(port) + "</td></tr>";
  page += "<tr><th>NTRIP Mountpoint</th><td>" + html_escape(mount) + "</td></tr>";
  page += "<tr><th>NTRIP User</th><td>" + html_escape(user) + "</td></tr>";
  page += "<tr><th>NTRIP Password</th><td>" + html_escape(ntrip_pass_mask) + "</td></tr>";
  page += "</table></div>";

  page += "<div class='card'><h3>Edit Configuration</h3>";
  page += "<form id='cfgForm' method='POST' action='/config'>";
  page += "<label>Wi-Fi SSID</label><input id='ssid' name='ssid' maxlength='32' value='" + html_escape(ssid) + "' required>";
  page += "<label>Wi-Fi Password</label><input id='pass' name='pass' maxlength='64' type='password' value=''>";
  page += "<div class='muted'>Leave blank to keep current saved Wi-Fi password.</div>";
  page += "<label>NTRIP Host (leave host+mount blank to disable)</label><input id='ntrip_host' name='ntrip_host' maxlength='63' value='" + html_escape(host) + "'>";
  page += "<label>NTRIP Port</label><input id='ntrip_port' name='ntrip_port' type='number' min='1' max='65535' value='" + html_escape(port) + "' required>";
  page += "<label>NTRIP Mountpoint</label><input id='ntrip_mount' name='ntrip_mount' maxlength='63' value='" + html_escape(mount) + "'>";
  page += "<label>NTRIP User</label><input id='ntrip_user' name='ntrip_user' maxlength='63' value='" + html_escape(user) + "'>";
  page += "<label>NTRIP Password</label><input id='ntrip_pass' name='ntrip_pass' maxlength='63' type='password' value=''>";
  page += "<div class='muted'>Leave blank to keep current saved NTRIP password.</div>";
  page += "<button type='submit'>Save Config and Reboot</button>";
  page += "<button type='button' id='testBtn'>Test NTRIP Connection (No Reboot)</button>";
  page += "</form><pre id='testResult'>No test yet.</pre></div>";

  page += "<div class='card'><h3>Danger Zone</h3>";
  page += "<form method='POST' action='/delete_wifi' onsubmit='return confirm(\"Delete saved Wi-Fi and reboot?\");'>";
  page += "<button type='submit'>Delete Saved Wi-Fi</button></form>";
  page += "<form method='POST' action='/delete_ntrip' onsubmit='return confirm(\"Delete saved NTRIP and reboot?\");'>";
  page += "<button type='submit'>Delete Saved NTRIP</button></form></div>";

  page += "<script>";
  page += "document.getElementById('testBtn').addEventListener('click', async ()=>{";
  page += "const p=new URLSearchParams();";
  page += "['ntrip_host','ntrip_port','ntrip_mount','ntrip_user','ntrip_pass'].forEach(id=>p.append(id,document.getElementById(id).value));";
  page += "const out=document.getElementById('testResult');out.textContent='Testing...';";
  page += "try{const r=await fetch('/api/ntrip_test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});";
  page += "const t=await r.text();out.textContent=t;}catch(e){out.textContent='Test failed: '+e;}});";
  page += "</script></body></html>";

  g_server.send(200, "text/html", page);
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
      ntrip_host.length() > 63 || ntrip_mount.length() > 63 || ntrip_user.length() > 63 ||
      ntrip_pass.length() > 63 || parsed_port <= 0 || parsed_port > 65535 || ntrip_partial) {
    g_server.send(400, "text/plain", "Invalid config values");
    return;
  }

  if (g_cfg == nullptr) {
    g_server.send(500, "text/plain", "Config not initialized");
    return;
  }

  String final_pass = pass;
  if (final_pass.length() == 0) {
    final_pass = String(g_cfg->wifi_pass);
  }

  String final_ntrip_pass = ntrip_pass;
  if (final_ntrip_pass.length() == 0) {
    final_ntrip_pass = String(g_cfg->ntrip_pass);
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

  g_server.send(200, "text/html", "<html><body><h3>Saved. Rebooting...</h3></body></html>");
  delay(600);
  ESP.restart();
}

static void handle_api_ntrip_test() {
  String host = g_server.arg("ntrip_host");
  String port_s = g_server.arg("ntrip_port");
  String mount = g_server.arg("ntrip_mount");
  String user = g_server.arg("ntrip_user");
  String pass = g_server.arg("ntrip_pass");

  host.trim();
  port_s.trim();
  mount.trim();
  user.trim();
  pass.trim();

  if (host.length() == 0 || mount.length() == 0) {
    g_server.send(400, "text/plain", "NTRIP test failed: host and mountpoint are required.");
    return;
  }

  long port_l = port_s.toInt();
  if (port_l <= 0 || port_l > 65535) {
    g_server.send(400, "text/plain", "NTRIP test failed: invalid port.");
    return;
  }

  const char* mount_ptr = mount.c_str();
  if (mount_ptr[0] == '/') {
    mount_ptr++;
  }

  String auth_input = user + ":" + pass;
  String auth_b64 = base64_encode(auth_input.c_str());

  WiFiClient test_client;
  test_client.setTimeout(2);
  uint32_t start_ms = millis();
  if (!test_client.connect(host.c_str(), static_cast<uint16_t>(port_l))) {
    g_server.send(200, "text/plain", "NTRIP test failed: socket connect failed.");
    return;
  }

  String req;
  req.reserve(512);
  req += "GET /";
  req += mount_ptr;
  req += " HTTP/1.1\r\n";
  req += "Host: ";
  req += host;
  req += ":";
  req += String(port_l);
  req += "\r\n";
  req += "Ntrip-Version: Ntrip/2.0\r\n";
  req += "User-Agent: NTRIP ESP32-Rover/Test\r\n";
  req += "Accept: */*\r\n";
  req += "Connection: close\r\n";
  req += "Authorization: Basic ";
  req += auth_b64;
  req += "\r\n\r\n";
  test_client.print(req);

  String rx;
  rx.reserve(200);
  const uint32_t deadline = millis() + 3000;
  while (millis() < deadline && rx.length() < 180) {
    while (test_client.available() > 0 && rx.length() < 180) {
      char c = static_cast<char>(test_client.read());
      if (c >= 32 && c <= 126) {
        rx += c;
      } else if (c == '\r' || c == '\n') {
        rx += ' ';
      }
    }
    delay(5);
  }
  test_client.stop();

  const bool ok = (rx.indexOf("ICY 200") >= 0) || (rx.indexOf(" 200 ") >= 0);
  const uint32_t elapsed = millis() - start_ms;
  String msg;
  msg.reserve(320);
  if (ok) {
    msg += "NTRIP test OK (";
    msg += String(elapsed);
    msg += " ms). Header: ";
  } else {
    msg += "NTRIP test failed (";
    msg += String(elapsed);
    msg += " ms). Header: ";
  }
  if (rx.length() == 0) {
    msg += "(no response bytes)";
  } else {
    msg += rx;
  }

  g_server.send(200, "text/plain", msg);
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
  g_server.on("/", HTTP_GET, handle_redirect_status);
  g_server.on("/config", HTTP_GET, handle_config_page);
  g_server.on("/status", HTTP_GET, handle_status_page);
  g_server.on("/api/status", HTTP_GET, handle_api_status);
  g_server.on("/api/ntrip_test", HTTP_POST, handle_api_ntrip_test);
  g_server.on("/config", HTTP_POST, handle_config_save);
  g_server.on("/wifi", HTTP_POST, handle_config_save);
  g_server.on("/delete_wifi", HTTP_POST, handle_delete_wifi);
  g_server.on("/delete_ntrip", HTTP_POST, handle_delete_ntrip);

  g_server.on("/generate_204", HTTP_GET, handle_redirect_status);
  g_server.on("/hotspot-detect.html", HTTP_GET, handle_redirect_status);
  g_server.on("/ncsi.txt", HTTP_GET, handle_redirect_status);
  g_server.onNotFound(handle_redirect_status);

  g_server.begin();
  server_active_ = true;
  LOGI("Portal server started");
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
    if (ap_active_) {
      stop_ap_();
      WiFi.mode(WIFI_STA);
      if (!server_active_) {
        start_server_();
      }
    }
    prev_wifi_connected_ = true;
    wifi_down_since_ms_ = 0;
  } else {
    const bool wifi_config_missing = (cfg_ == nullptr || cfg_->wifi_ssid[0] == '\0');
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
  }

  if (server_active_) {
    g_server.handleClient();
  }
}

bool ApPortal::ap_active() const { return ap_active_; }
