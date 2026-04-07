#include "admin_panel.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Updater.h>
#include <stdio.h>
#include <string.h>

#include "captive_portal.h"
#include "config_storage.h"
#include "session_manager.h"
#include "version.h"

static char uptime_buf[32];

static bool copy_non_empty_form_field(AsyncWebServerRequest *request,
                                      const char *name, char *dst,
                                      size_t dst_size) {
  if (request == nullptr || name == nullptr || dst == nullptr || dst_size == 0 ||
      !request->hasParam(name, true)) {
    return false;
  }

  const AsyncWebParameter *param = request->getParam(name, true);
  if (param == nullptr) {
    return false;
  }

  const String &value = param->value();
  if (value.length() == 0) {
    return false;
  }

  strncpy(dst, value.c_str(), dst_size - 1);
  dst[dst_size - 1] = '\0';
  return true;
}

static void html_escape(const char *src, char *dst, size_t dst_size) {
  if (dst_size == 0) {
    return;
  }

  size_t out = 0;
  for (size_t i = 0; src && src[i] != '\0' && out + 1 < dst_size; i++) {
    const char *rep = nullptr;
    switch (src[i]) {
      case '&':
        rep = "&amp;";
        break;
      case '<':
        rep = "&lt;";
        break;
      case '>':
        rep = "&gt;";
        break;
      case '"':
        rep = "&quot;";
        break;
      case '\'':
        rep = "&#39;";
        break;
      default:
        break;
    }

    if (rep != nullptr) {
      size_t rep_len = strlen(rep);
      if (out + rep_len >= dst_size) {
        break;
      }
      memcpy(&dst[out], rep, rep_len);
      out += rep_len;
    } else {
      dst[out++] = src[i];
    }
  }
  dst[out] = '\0';
}

static const char *format_uptime(unsigned long seconds) {
  unsigned long d = seconds / 86400;
  seconds %= 86400;
  unsigned long h = seconds / 3600;
  seconds %= 3600;
  unsigned long m = seconds / 60;
  unsigned long s = seconds % 60;

  if (d > 0) {
    snprintf(uptime_buf, sizeof(uptime_buf), "%lud %luh %lum %lus", d, h, m, s);
  } else if (h > 0) {
    snprintf(uptime_buf, sizeof(uptime_buf), "%luh %lum %lus", h, m, s);
  } else if (m > 0) {
    snprintf(uptime_buf, sizeof(uptime_buf), "%lum %lus", m, s);
  } else {
    snprintf(uptime_buf, sizeof(uptime_buf), "%lus", s);
  }
  return uptime_buf;
}

static const char *resolve_admin_tab(const String &tab,
                                     const char *fallback_tab) {
  if (tab == "tb-net") {
    return "tb-net";
  }
  if (tab == "tb-vcn") {
    return "tb-vcn";
  }
  if (tab == "tb-sys") {
    return "tb-sys";
  }
  if (tab == "tb-dsh") {
    return "tb-dsh";
  }
  return fallback_tab;
}

static const char *get_admin_tab_param(AsyncWebServerRequest *request,
                                       bool from_post_body,
                                       const char *fallback_tab) {
  if (request != nullptr && request->hasParam("tab", from_post_body)) {
    return resolve_admin_tab(request->getParam("tab", from_post_body)->value(),
                             fallback_tab);
  }
  return fallback_tab;
}

void admin_panel_init() {
  AsyncWebServer *server = get_web_server();

  server->on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(global_config.admin_user,
                               global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    bool config_changed = false;
    ClientSession *sessions = session_manager_get_sessions();
    const char *selected_tab = get_admin_tab_param(request, false, "tb-dsh");
    const bool is_tab_dsh = strcmp(selected_tab, "tb-dsh") == 0;
    const bool is_tab_net = strcmp(selected_tab, "tb-net") == 0;
    const bool is_tab_vcn = strcmp(selected_tab, "tb-vcn") == 0;
    const bool is_tab_sys = strcmp(selected_tab, "tb-sys") == 0;
    const char *tab_title = is_tab_net ? "Network"
                            : is_tab_vcn ? "Vouchers"
                            : is_tab_sys ? "System"
                                         : "Dashboard";

    for (int i = 0; i < VOUCHER_COUNT; i++) {
      if (global_config.vouchers[i].is_used) {
        bool is_active = false;
        for (int j = 0; j < MAX_SESSIONS; j++) {
          if (sessions[j].active &&
              strcmp(sessions[j].voucher_code,
                     global_config.vouchers[i].code) == 0) {
            is_active = true;
            break;
          }
        }
        if (!is_active) {
          config_storage_generate_random(global_config.vouchers[i].code, 6);
          global_config.vouchers[i].is_used = false;
          config_changed = true;
        }
      } else if (strlen(global_config.vouchers[i].code) == 0) {
        config_storage_generate_random(global_config.vouchers[i].code, 6);
        global_config.vouchers[i].is_used = false;
        config_changed = true;
      }
    }

    if (config_changed) {
      config_storage_save();
    }

    bool wifi_connected = WiFi.status() == WL_CONNECTED;
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    char esc_sta_ssid[192];
    char esc_ap_ssid[192];
    char esc_ap_ip[96];
    char esc_admin_user[192];
    char esc_uplink_ssid[192];
    html_escape(global_config.sta_ssid, esc_sta_ssid, sizeof(esc_sta_ssid));
    html_escape(global_config.ap_ssid, esc_ap_ssid, sizeof(esc_ap_ssid));
    html_escape(global_config.ap_ip, esc_ap_ip, sizeof(esc_ap_ip));
    html_escape(global_config.admin_user, esc_admin_user,
                sizeof(esc_admin_user));
    const String current_uplink_ssid = WiFi.SSID();
    if (wifi_connected && current_uplink_ssid.length() > 0) {
      html_escape(current_uplink_ssid.c_str(), esc_uplink_ssid,
                  sizeof(esc_uplink_ssid));
    } else {
      html_escape(global_config.sta_ssid, esc_uplink_ssid,
                  sizeof(esc_uplink_ssid));
    }

    response->print(F(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>MiniRouter Admin</title>"
        "<style>"
        "*{box-sizing:border-box}body{margin:0;font-family:system-ui,-apple-system,'Segoe UI',Roboto,Arial,sans-serif;background:#f1f5f9;color:#0f172a}"
        ".wrap{display:flex;min-height:100vh}.side{width:220px;background:#0f172a;color:#cbd5e1;padding:16px 10px}.logo{font-size:20px;font-weight:800;margin:0 8px 14px}"
        ".nav-btn{display:block;padding:10px 12px;border-radius:8px;color:#cbd5e1;text-decoration:none;font-weight:700;margin-bottom:6px}.nav-btn.active{background:#1d4ed8;color:#fff}"
        ".main{flex:1;padding:22px}.top{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px}.top h2{margin:0;font-size:22px}"
        ".status{font-size:12px;font-weight:700;background:#fff;border:1px solid #cbd5e1;padding:6px 10px;border-radius:999px}.dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}.dot.on{background:#16a34a}.dot.off{background:#dc2626}"
        ".pane{display:none}.pane.active{display:block}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}"
        ".card{background:#fff;border:1px solid #cbd5e1;border-radius:10px;padding:16px;margin-bottom:16px}.card h3{margin:0 0 6px;font-size:16px}.card-head{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:12px}.card-desc{margin:0;color:#64748b;font-size:13px;line-height:1.45}"
        ".section-label{font-size:10px;font-weight:800;letter-spacing:.4px;text-transform:uppercase;background:#e2e8f0;padding:3px 7px;border-radius:999px}.top-stats{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}"
        ".stat-box{border:1px solid #cbd5e1;border-radius:8px;padding:8px;background:#f8fafc}.stat-box h4{margin:0 0 4px;font-size:11px;color:#64748b;text-transform:uppercase}.stat-box p{margin:0;font-size:16px;font-weight:800}"
        ".table-wrap{overflow:auto;border:1px solid #cbd5e1;border-radius:8px}table{width:100%;border-collapse:collapse}th,td{padding:10px 12px;border-bottom:1px solid #cbd5e1;font-size:12px;vertical-align:middle}th{font-size:11px;text-transform:uppercase;color:#64748b;background:#f8fafc;text-align:left}tr:last-child td{border-bottom:0}"
        ".active-table th,.active-table td{text-align:left}.active-table th:last-child,.active-table td:last-child{width:120px;text-align:left}"
        ".voucher-table th,.voucher-table td{text-align:left;vertical-align:middle}"
        ".voucher-table th:last-child,.voucher-table td:last-child{width:140px;text-align:left}"
        ".form-group{margin-bottom:12px}.form-group p{margin:0;font-weight:700}label{display:block;font-size:11px;color:#64748b;margin-bottom:5px;text-transform:uppercase;font-weight:700}"
        ".kv{border:1px solid #cbd5e1;border-radius:8px}.kv-row{display:flex;justify-content:space-between;padding:8px 10px;border-bottom:1px solid #cbd5e1}.kv-row:last-child{border-bottom:0}.kv-label{font-size:11px;color:#64748b;text-transform:uppercase;font-weight:700}.kv-value{font-size:13px;font-weight:700}.kv-value.mono{font-family:ui-monospace,Menlo,Consolas,monospace}"
        "input{width:100%;padding:9px;border:1px solid #cbd5e1;border-radius:8px;font-size:14px}.btn{border:0;border-radius:8px;background:#2563eb;color:#fff;padding:10px 12px;font-weight:700}.btn.full{width:100%}.btn.sec{background:#e2e8f0;color:#0f172a}.btn.dng{background:#dc2626}"
        ".badge{padding:3px 7px;border-radius:999px;font-size:10px;font-weight:800;text-transform:uppercase}.badge.suc{background:#dcfce7;color:#15803d}.badge.use{background:#fee2e2;color:#b91c1c}.badge.neu{background:#e2e8f0;color:#334155}"
        ".kick-btn{border:0;background:transparent;color:#dc2626;font-weight:700}.empty{padding:10px;text-align:center;color:#64748b;font-weight:700}.danger-zone{border-color:#fecaca;background:#fff7f7}.danger-note{margin:0 0 10px;color:#b45309;font-size:13px;font-weight:700}"
        "@media(max-width:900px){.wrap{display:block}.side{width:100%}.nav-btn{display:inline-block;margin:0 6px 8px 0}.main{padding:16px}.grid{grid-template-columns:1fr}.top-stats{grid-template-columns:1fr}}"
        "</style></head><body><div class='wrap'>"
        "<aside class='side'><div class='logo'>MiniRouter OS</div>"));
    response->printf(
        "<a id='b-tb-dsh' class='nav-btn%s' href='/admin?tab=tb-dsh' "
        "data-tab='tb-dsh'>Dashboard</a>",
        is_tab_dsh ? " active" : "");
    response->printf(
        "<a id='b-tb-net' class='nav-btn%s' href='/admin?tab=tb-net' "
        "data-tab='tb-net'>Network</a>",
        is_tab_net ? " active" : "");
    response->printf(
        "<a id='b-tb-vcn' class='nav-btn%s' href='/admin?tab=tb-vcn' "
        "data-tab='tb-vcn'>Vouchers</a>",
        is_tab_vcn ? " active" : "");
    response->printf(
        "<a id='b-tb-sys' class='nav-btn%s' href='/admin?tab=tb-sys' "
        "data-tab='tb-sys'>System</a>",
        is_tab_sys ? " active" : "");
    response->printf(
        "</aside><main class='main'><div class='top'><h2 id='mtit' "
        "style='margin:0'>%s</h2>",
        tab_title);
    response->printf("<div class='status'><span class='dot %s'></span>%s</div>",
                     wifi_connected ? "on" : "off",
                     wifi_connected ? "Upstream Online" : "Upstream Offline");
    response->print(F("</div>"));

    if (is_tab_dsh) {
      response->print("<div id='tb-dsh' class='pane active'><div class='grid'>");

    // Card 1: System Quick Stats
    response->print(
        F("<div class='card' style='grid-column: span 2'><div "
          "class='card-head'><div><h3>System Summary</h3><p "
          "class='card-desc'>Firmware version, uptime, and free memory.</p></div>"
          "<span class='section-label'>Summary</span></div>"));
    response->printf(
        "<div class='top-stats'>"
        "<div class='stat-box'><h4>Firmware</h4><p>v%s</p></div>"
        "<div class='stat-box'><h4>Uptime</h4><p>%s</p></div>"
        "<div class='stat-box'><h4>Free Memory</h4><p>%d KB</p></div>"
        "</div></div>",
        FIRMWARE_VERSION, format_uptime(millis() / 1000),
        ESP.getFreeHeap() / 1024);

    // Card 2: Active Clients
    response->print(
        F("<div class='card'><div class='card-head'><div><h3>Active "
          "Clients</h3><p class='card-desc'>Current authenticated client "
          "sessions.</p></div><span "
          "class='section-label'>Sessions</span></div><div "
          "class='table-wrap'><table class='active-table'><tr><th>IP</th><th>MAC</"
          "th><th>Voucher</th><th>Expires</th><th>Actions</th></tr>"));

    int active_count = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (sessions[i].active) {
        active_count++;
        unsigned long time_left =
            SESSION_TIMEOUT_MS - (millis() - sessions[i].timestamp);
        response->printf("<tr><td>%d.%d.%d.%d</td>", sessions[i].ip[0],
                         sessions[i].ip[1], sessions[i].ip[2],
                         sessions[i].ip[3]);

        if (sessions[i].has_mac) {
          response->printf("<td>%02X:%02X:%02X:%02X:%02X:%02X</td>",
                           sessions[i].mac[0], sessions[i].mac[1],
                           sessions[i].mac[2], sessions[i].mac[3],
                           sessions[i].mac[4], sessions[i].mac[5]);
        } else {
          response->print(F("<td>Unknown</td>"));
        }

        if (strcmp(sessions[i].voucher_code, "ADMIN") == 0) {
          response->print(F("<td><span class='badge neu'>ADMIN</span></td>"));
        } else {
          char esc_session_voucher[96];
          html_escape(sessions[i].voucher_code, esc_session_voucher,
                      sizeof(esc_session_voucher));
          response->printf("<td><span class='badge suc'>%s</span></td>",
                           esc_session_voucher);
        }

        response->printf("<td>%s</td>", format_uptime(time_left / 1000));
        response->printf("<td><form action='/admin/kick' method='POST' "
                         "style='margin:0'>"
                         "<input type='hidden' name='tab' value='tb-dsh'>"
                         "<input type='hidden' name='ip' value='%d.%d.%d.%d'>"
                         "<button type='submit' "
                         "class='kick-btn'>Disconnect</button>"
                         "</form></td></tr>",
                         sessions[i].ip[0], sessions[i].ip[1],
                         sessions[i].ip[2], sessions[i].ip[3]);
      }
    }

    if (active_count == 0) {
      response->print(
          F("<tr><td colspan='5' class='empty'>No active client sessions.</td></tr>"));
    }
    response->print(F("</table></div></div>"));

    // Card 3: Network Status
    response->print(
        F("<div class='card'><div class='card-head'><div><h3>Network "
          "Status</h3><p class='card-desc'>Current IP addresses and "
          "signal level.</p></div><span "
          "class='section-label'>Network</span></div>"));
    const String ap_ip = WiFi.softAPIP().toString();
    const String uplink_ip = WiFi.localIP().toString();
    const int rssi_dbm = WiFi.RSSI();
    response->print(F("<div class='kv'>"));
    response->printf(
        "<div class='kv-row'><span class='kv-label'>Connected Uplink SSID</span>"
        "<span class='kv-value'>%s</span></div>",
        esc_uplink_ssid);
    response->printf(
        "<div class='kv-row'><span class='kv-label'>Local AP IP</span>"
        "<span class='kv-value mono'>%s</span></div>",
        ap_ip.c_str());
    response->printf(
        "<div class='kv-row'><span class='kv-label'>Uplink IP</span>"
        "<span class='kv-value mono'>%s</span></div>",
        uplink_ip.c_str());
    response->printf(
        "<div class='kv-row'><span class='kv-label'>WiFi Signal</span>"
        "<span class='kv-value'>%d dBm</span></div>",
        rssi_dbm);
    response->print(F("</div></div>"));

    response->print(
        F("<div class='card' style='grid-column: span 2'><div class='card-head'><div><h3>Available "
          "Voucher Codes</h3><p class='card-desc'>Codes that can be used "
          "for new client login.</p></div><span "
          "class='section-label'>Vouchers</span></div><div>"));

    int available_count = 0;
    for (int i = 0; i < VOUCHER_COUNT; i++) {
      if (!global_config.vouchers[i].is_used) {
        char esc_voucher[96];
        html_escape(global_config.vouchers[i].code, esc_voucher,
                    sizeof(esc_voucher));
        response->printf("<span class='badge suc' style='display:inline-block;"
                         "margin:0 8px 8px 0'>%s</span>",
                         esc_voucher);
        available_count++;
      }
    }
    if (available_count == 0) {
      response->print(F("<p class='empty' style='margin:0'>No available voucher "
                        "codes.</p>"));
    }
    response->print(F("</div></div>"));

      response->print(F("</div></div>")); // Close grid and tb-dsh
    }

    if (is_tab_net) {
      response->print(
          F("<div id='tb-net' class='pane active'><form action='/admin/config' "
            "method='POST'><input type='hidden' name='tab' value='tb-net'>"));
      response->print(
          F("<div class='card'><div class='card-head'><div><h3>Internet "
            "Uplink</h3><p class='card-desc'>Connect this router "
            "to your main internet source.</p></div><span "
            "class='section-label'>WAN</span></div><div class='grid'>"));
      response->printf(
          "<div class='form-group'><label>Uplink WiFi SSID</label><input "
          "type='text' name='ssid' value='%s' required></div>",
          esc_sta_ssid);
      response->print(
          F("<div class='form-group'><label>Uplink WiFi Password</label><input "
            "type='password' name='pass' placeholder='Leave blank to keep "
            "current password'></div>"));
      response->print(F("</div></div>"));

      response->print(
          F("<div class='card'><div class='card-head'><div><h3>Local Access "
            "Point</h3><p class='card-desc'>Configure WiFi SSID and LAN "
            "gateway served to clients.</p></div><span "
            "class='section-label'>LAN</span></div><div class='grid'>"));
      response->printf(
          "<div class='form-group'><label>AP SSID</label><input "
          "type='text' name='ap_ssid' value='%s' required></div>",
          esc_ap_ssid);
      response->print(
          F("<div class='form-group'><label>AP Password</label><input "
            "type='password' name='ap_pass' placeholder='Leave blank to keep "
            "current password'></div>"));
      response->printf(
          "<div class='form-group'><label>AP IP Address (Gateway)</label><input "
          "type='text' name='ap_ip' value='%s' required></div>",
          esc_ap_ip);
      response->print(F("</div></div>"));
      response->print(
          F("<button type='submit' class='btn full'>Save Network "
            "Settings</button></form></div>"));
    }

    if (is_tab_vcn) {
      response->print(
          F("<div id='tb-vcn' class='pane active'><div class='card'><div "
            "class='card-head'><div><h3>Voucher Pool</h3><p "
            "class='card-desc'>Latest generated 6-digit access codes "
            "with real-time usage status.</p></div><span "
            "class='section-label'>Access</span></div><div "
            "class='table-wrap'><table class='voucher-table'><tr><th>Code</"
            "th><th>Status</th></tr>"));
      for (int i = 0; i < VOUCHER_COUNT; i++) {
        char esc_voucher[96];
        html_escape(global_config.vouchers[i].code, esc_voucher,
                    sizeof(esc_voucher));
        response->printf("<tr><td><strong>%s</strong></td>", esc_voucher);
        if (global_config.vouchers[i].is_used) {
          response->print(F("<td><span class='badge use'>In Use</span></td></tr>"));
        } else {
          response->print(
              F("<td><span class='badge suc'>Available</span></td></tr>"));
        }
      }
      response->print(F("</table></div></div></div>"));
    }

    if (is_tab_sys) {
      response->print(
          F("<div id='tb-sys' class='pane active'><form action='/admin/config' "
            "method='POST'>"
            "<input type='hidden' name='tab' value='tb-sys'>"
            "<div class='card'><div class='card-head'><div><h3>Administrator "
            "Credentials</h3><p class='card-desc'>Update credentials to secure "
            "this dashboard.</p></div><span "
            "class='section-label'>Security</span></div><div class='grid'>"));
      response->printf(
          "<div class='form-group'><label>Admin Username</label><input "
          "type='text' name='admin_user' value='%s' required></div>",
          esc_admin_user);
      response->print(
          F("<div class='form-group'><label>Admin Password</label><input "
            "type='password' name='admin_pass' placeholder='Leave blank to keep "
            "current password'></div>"));
      response->print(
          F("</div><button type='submit' class='btn full' "
            "style='margin-bottom:10px;'>Save Administrator "
            "Credentials</button></div></form>"));

      response->print(F(
          "<div class='card'><div class='card-head'><div><h3>System "
          "Maintenance</h3><p class='card-desc'>Apply OTA update and restart "
          "the router safely.</p></div><span "
          "class='section-label'>Maintenance</span></div>"));
      response->print(F(
          "<form action='/admin/update' method='POST' "
          "enctype='multipart/form-data'><input type='hidden' name='tab' "
          "value='tb-sys'><div class='form-group'>"
          "<label>OTA Firmware Update (.bin)</label><input type='file' "
          "name='update' accept='.bin' required></div>"
          "<button type='submit' class='btn full' "
          "style='margin-bottom:20px'>Upload and Flash Firmware</button></form>"));
      response->print(F(
          "<form action='/admin/reboot' method='POST'><input type='hidden' "
          "name='tab' value='tb-sys'><button type='submit' "
          "class='btn full sec'>Reboot Device</button></form></div>"));

      response->print(
          F("<div class='card danger-zone'><div class='card-head'><div><h3>"
            "Danger Zone</h3><p class='card-desc'>Permanent actions that "
            "remove current configuration.</p></div><span "
            "class='section-label'>Critical</span></div><p "
            "class='danger-note'>Factory reset will wipe all settings and "
            "reboot immediately.</p><form action='/admin/reset' method='POST'>"
            "<input type='hidden' name='tab' value='tb-sys'>"));
      response->print(
          F("<div class='form-group'><label>Type RESET to Confirm</label>"
            "<input type='text' name='reset_confirm' placeholder='RESET' "
            "pattern='RESET' required></div>"));
      response->print(
          F("<button type='submit' class='btn full dng'"
            " onclick='return confirm(\"Run factory reset?\\n\\nAll settings "
            "will be lost and router will reboot!\")'>Factory Reset</button>"
            "</form></div></div>"));
    }

    response->print(F("</main></div></body></html>"));
    request->send(response);
  });

  server->on("/admin/kick", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(global_config.admin_user,
                               global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    const char *tab = get_admin_tab_param(request, true, "tb-dsh");
    if (request->hasParam("ip", true)) {
      IPAddress targetIP;
      if (targetIP.fromString(request->getParam("ip", true)->value())) {
        session_manager_logout(targetIP);
      }
    }
    char redirect_url[48];
    snprintf(redirect_url, sizeof(redirect_url), "/admin?tab=%s", tab);
    request->redirect(redirect_url);
  });

  server->on("/admin/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(global_config.admin_user,
                               global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    bool config_changed = false;
    config_changed |=
        copy_non_empty_form_field(request, "ssid", global_config.sta_ssid,
                                  sizeof(global_config.sta_ssid));
    config_changed |=
        copy_non_empty_form_field(request, "pass", global_config.sta_pass,
                                  sizeof(global_config.sta_pass));
    config_changed |=
        copy_non_empty_form_field(request, "ap_ssid", global_config.ap_ssid,
                                  sizeof(global_config.ap_ssid));
    config_changed |=
        copy_non_empty_form_field(request, "ap_pass", global_config.ap_pass,
                                  sizeof(global_config.ap_pass));
    config_changed |=
        copy_non_empty_form_field(request, "ap_ip", global_config.ap_ip,
                                  sizeof(global_config.ap_ip));
    config_changed |= copy_non_empty_form_field(
        request, "admin_user", global_config.admin_user,
        sizeof(global_config.admin_user));
    config_changed |= copy_non_empty_form_field(
        request, "admin_pass", global_config.admin_pass,
        sizeof(global_config.admin_pass));

    const char *tab = get_admin_tab_param(request, true, "tb-dsh");
    if (config_changed) {
      global_config.sta_ssid[sizeof(global_config.sta_ssid) - 1] = '\0';
      global_config.sta_pass[sizeof(global_config.sta_pass) - 1] = '\0';
      global_config.ap_ssid[sizeof(global_config.ap_ssid) - 1] = '\0';
      global_config.ap_pass[sizeof(global_config.ap_pass) - 1] = '\0';
      global_config.ap_ip[sizeof(global_config.ap_ip) - 1] = '\0';
      global_config.admin_user[sizeof(global_config.admin_user) - 1] = '\0';
      global_config.admin_pass[sizeof(global_config.admin_pass) - 1] = '\0';
      config_storage_save();

      request->send(200, "text/plain",
                    "Settings saved. Rebooting to apply changes...");
      delay(500);
      ESP.restart();
    } else {
      char redirect_url[48];
      snprintf(redirect_url, sizeof(redirect_url), "/admin?tab=%s", tab);
      request->redirect(redirect_url);
    }
  });

  server->on("/admin/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(global_config.admin_user,
                               global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    request->send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
  });

  server->on("/admin/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(global_config.admin_user,
                               global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    if (!request->hasParam("reset_confirm", true) ||
        request->getParam("reset_confirm", true)->value() != "RESET") {
      request->send(400, "text/plain",
                    "Factory reset blocked. Type RESET to confirm.");
      return;
    }

    request->send(200, "text/plain",
                  "Factory reset in progress. Rebooting in 3 seconds.");
    delay(500);
    config_storage_factory_reset();
  });

  server->on(
      "/admin/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (!request->authenticate(global_config.admin_user,
                                   global_config.admin_pass)) {
          return request->requestAuthentication();
        }

        bool shouldReboot = !Update.hasError();
        AsyncWebServerResponse *response =
            request->beginResponse(shouldReboot ? 200 : 500, "text/plain",
                                   shouldReboot ? "Update Success! Rebooting..."
                                                : "Update Failed! Try again.");
        response->addHeader("Connection", "close");
        request->send(response);

        if (shouldReboot) {
          delay(500);
          ESP.restart();
        }
      },
      [](AsyncWebServerRequest *request, String filename, size_t index,
         uint8_t *data, size_t len, bool final) {
        (void)filename;
        if (!request->authenticate(global_config.admin_user,
                                   global_config.admin_pass)) {
          return;
        }

        if (!index) {
#ifdef DEBUG
          Serial.printf("Update Start: %s\n", filename.c_str());
#endif
          Update.runAsync(true);
          if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
#ifdef DEBUG
            Update.printError(Serial);
#endif
          }
        }

        if (!Update.hasError()) {
          if (Update.write(data, len) != len) {
#ifdef DEBUG
            Serial.println("Update Write failed.");
            Update.printError(Serial);
#endif
          }
        }

        if (final) {
          if (Update.end(true)) {
#ifdef DEBUG
            Serial.printf("Update Success: %uB\n", index + len);
#endif
          } else {
#ifdef DEBUG
            Serial.println("Update Finalize failed.");
            Update.printError(Serial);
#endif
          }
        }
      });

#ifdef DEBUG
  Serial.println("[Admin] Admin dashboard routes registered at /admin");
#endif
}
