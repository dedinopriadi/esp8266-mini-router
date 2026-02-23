#include "admin_panel.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Updater.h>
#include <stdio.h>
#include <string.h>

#include "captive_portal.h"
#include "config_storage.h"
#include "html_templates.h"
#include "session_manager.h"

static char uptime_buf[32];

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

void admin_panel_init() {
  AsyncWebServer *server = get_web_server();

  server->on("/admin", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    bool config_changed = false;
    ClientSession *sessions = session_manager_get_sessions();

    for (int i = 0; i < 5; i++) {
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

    response->print(F(
        "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1\"><title>Router "
        "OS</"
        "title><style>*,::after,::before{box-sizing:border-box}body{font-"
        "family:system-ui,-apple-system,sans-serif;background-color:#f3f4f6;"
        "color:#111827;margin:0;padding:20px}.c{max-width:1000px;margin:0 "
        "auto}.h{display:flex;justify-content:space-between;align-items:center;"
        "background:#fff;padding:20px;border-radius:12px;box-shadow:0 1px 3px "
        "rgba(0,0,0,.1);margin-bottom:20px}.h "
        "h1{margin:0;font-size:24px;color:#2563eb}.h "
        ".st{display:flex;align-items:center;gap:10px}.g{display:grid;grid-"
        "template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px}.card{"
        "background:#fff;border-radius:12px;padding:20px;box-shadow:0 1px 3px "
        "rgba(0,0,0,.1)}.col-2{grid-column:1/-1}.card h3{margin:0 0 "
        "15px;padding-bottom:10px;border-bottom:1px solid "
        "#e5e7eb;color:#374151}table{width:100%;border-collapse:collapse;"
        "margin-bottom:10px}th,td{padding:10px;text-align:left;border-bottom:"
        "1px solid "
        "#e5e7eb;font-size:14px}th{font-weight:600;color:#6b7280}.bg{padding:"
        "4px "
        "8px;border-radius:999px;font-size:12px;font-weight:500;white-space:"
        "nowrap}.bg-act{background:#d1fae5;color:#065f46}.bg-avl{background:#"
        "e0e7ff;color:#3730a3}.bg-use{background:#fee2e2;color:#991b1b}.bg-adm{"
        "background:#fef3c7;color:#92400e}.st-ind{width:10px;height:10px;"
        "border-radius:50%;display:inline-block}.st-on{background:#10b981;box-"
        "shadow:0 0 8px #10b981}.st-off{background:#ef4444;box-shadow:0 0 8px "
        "#ef4444}label{display:block;margin-bottom:6px;font-size:14px;color:#"
        "4b5563;font-weight:500}input[type=text],input[type=password],input["
        "type=file]{width:100%;padding:10px;border:1px solid "
        "#d1d5db;border-radius:6px;margin-bottom:15px;font-size:14px}input:"
        "focus{outline:0;border-color:#3b82f6;box-shadow:0 0 0 3px "
        "rgba(59,130,246,.2)}.btn{background:#3b82f6;color:#fff;padding:10px "
        "15px;border:none;border-radius:6px;font-size:14px;font-weight:500;"
        "cursor:pointer;width:100%;transition:.2s}.btn:hover{background:#"
        "2563eb}.btn.dng{background:#ef4444}.btn.dng:hover{background:#dc2626}<"
        "/style></head><body><div class=\"c\"><div class=\"h\"><h1>Router "
        "OS</h1>"));

    response->print(F("<div class=\"st\">"));
    response->printf("<span "
                     "style=\"font-size:14px;color:#4b5563;font-weight:500;"
                     "margin-right:15px;\">⏱ Uptime: %s</span>",
                     format_uptime(millis() / 1000));

    if (wifi_connected) {
      response->print(
          F("<span class=\"st-ind st-on\"></span><span "
            "style=\"font-size:14px;color:#4b5563;font-weight:500\">Upstream "
            "Online</span></div>"));
    } else {
      response->print(
          F("<span class=\"st-ind st-off\"></span><span "
            "style=\"font-size:14px;color:#4b5563;font-weight:500\">Upstream "
            "Offline</span></div>"));
    }

    response->print(F("</div><div class=\"g\">"));

    response->print(
        F("<div class='card col-2'><h3>Active Clients</h3><div "
          "style='overflow-x:auto'><table><tr><th>IP</th><th>MAC</"
          "th><th>Voucher</th><th>Expires</th><th>Action</th></tr>"));

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
          response->print(F("<td><span class='bg bg-adm'>ADMIN</span></td>"));
        } else {
          response->printf("<td><span class='bg bg-act'>%s</span></td>",
                           sessions[i].voucher_code);
        }

        response->printf("<td>%s</td>", format_uptime(time_left / 1000));
        response->printf("<td><a href='/admin/kick?ip=%d.%d.%d.%d' "
                         "style='color:#ef4444;text-decoration:none;font-"
                         "weight:600'>Kick</a></td></tr>",
                         sessions[i].ip[0], sessions[i].ip[1],
                         sessions[i].ip[2], sessions[i].ip[3]);
      }
    }

    if (active_count == 0) {
      response->print(F("<tr><td colspan='5' style='text-align:center'>No "
                        "active clients</td></tr>"));
    }

    response->print(F("</table></div></div>"));

    response->print(F("<div class='card'><h3>Voucher System (Auto)</h3><div "
                      "style='overflow-x:auto'><table><tr><th>Code</"
                      "th><th>Status</th></tr>"));
    for (int i = 0; i < 5; i++) {
      response->printf("<tr><td><strong>%s</strong></td>",
                       global_config.vouchers[i].code);
      if (global_config.vouchers[i].is_used) {
        response->print(
            F("<td><span class='bg bg-use'>In Use</span></td></tr>"));
      } else {
        response->print(
            F("<td><span class='bg bg-avl'>Available</span></td></tr>"));
      }
    }

    response->print(F("</table></div></div>"));

    response->print(F("<div class='card'><h3>Network Settings</h3><form "
                      "action='/admin/config' method='POST'>"));
    response->print(F("<h4>Upstream WiFi (Internet)</h4>"));
    response->printf("<label>SSID</label><input type='text' "
                     "name='ssid' value='%s' required>",
                     global_config.sta_ssid);
    response->printf("<label>Password</label><input type='password' "
                     "name='pass' value='%s'>",
                     global_config.sta_pass);

    response->print(
        F("<h4 style='margin-top:20px'>Local WiFi (Access Point)</h4>"));
    response->printf("<label>AP SSID</label><input type='text' "
                     "name='ap_ssid' value='%s' required>",
                     global_config.ap_ssid);
    response->printf("<label>AP Password</label><input type='password' "
                     "name='ap_pass' value='%s'>",
                     global_config.ap_pass);

    response->print(
        F("<button type='submit' class='btn' style='margin-top:15px'>Save "
          "Network</button></form></div>"));

    response->print(
        F("<div class='card'><h3>Maintenance</h3><form action='/admin/update' "
          "method='POST' enctype='multipart/form-data'>"));
    response->print(F("<label>OTA Firmware Update (.bin)</label><input "
                      "type='file' name='update' accept='.bin' required>"));
    response->print(
        F("<button type='submit' class='btn' style='margin-bottom:15px'>Flash "
          "Firmware</button></form>"));

    response->print(F("<div style='display:flex; gap:10px;'><form "
                      "action='/admin/reboot' method='POST' "
                      "style='flex:1;'><button type='submit' "
                      "class='btn'>Reboot Router</button></form>"));
    response->print(
        F("<form action='/admin/reset' method='POST' style='flex:1;'><button "
          "type='submit' "
          "class='btn dng' onclick='return confirm(\"Are you sure you want to "
          "Factory Reset?\\n\\nAll settings will be lost and router will "
          "reboot!\")'>Factory Reset</button></form></div></div>"));

    response->print(FPSTR(admin_footer_html));
    request->send(response);
  });

  server->on("/admin/kick", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    if (request->hasParam("ip")) {
      IPAddress targetIP;
      if (targetIP.fromString(request->getParam("ip")->value())) {
        session_manager_logout(targetIP);
      }
    }
    request->redirect("/admin");
  });

  server->on("/admin/config", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    bool config_changed = false;
    if (request->hasParam("ssid", true)) {
      strncpy(global_config.sta_ssid,
              request->getParam("ssid", true)->value().c_str(),
              sizeof(global_config.sta_ssid) - 1);
      config_changed = true;
      if (request->hasParam("pass", true)) {
        strncpy(global_config.sta_pass,
                request->getParam("pass", true)->value().c_str(),
                sizeof(global_config.sta_pass) - 1);
      }
    }

    if (request->hasParam("ap_ssid", true)) {
      strncpy(global_config.ap_ssid,
              request->getParam("ap_ssid", true)->value().c_str(),
              sizeof(global_config.ap_ssid) - 1);
      config_changed = true;
      if (request->hasParam("ap_pass", true)) {
        strncpy(global_config.ap_pass,
                request->getParam("ap_pass", true)->value().c_str(),
                sizeof(global_config.ap_pass) - 1);
      }
    }

    if (config_changed) {
      global_config.sta_ssid[sizeof(global_config.sta_ssid) - 1] = '\0';
      global_config.sta_pass[sizeof(global_config.sta_pass) - 1] = '\0';
      global_config.ap_ssid[sizeof(global_config.ap_ssid) - 1] = '\0';
      global_config.ap_pass[sizeof(global_config.ap_pass) - 1] = '\0';
      config_storage_save();

      request->send(200, "text/plain",
                    "Config Saved. Rebooting to apply changes...");
      delay(500);
      ESP.restart();
    } else {
      request->send(400, "text/plain", "Missing STA or AP SSID parameter");
    }
  });

  server->on("/admin/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    request->send(200, "text/plain", "Rebooting...");
    delay(500);
    ESP.restart();
  });

  server->on("/admin/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate("admin", global_config.admin_pass)) {
      return request->requestAuthentication();
    }

    request->send(200, "text/plain",
                  "FACTORY RESET IN PROGRESS... Rebooting in 3 seconds.");
    delay(500);
    config_storage_factory_reset();
  });

  server->on(
      "/admin/update", HTTP_POST,
      [](AsyncWebServerRequest *request) {
        if (!request->authenticate("admin", global_config.admin_pass)) {
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
        if (!request->authenticate("admin", global_config.admin_pass)) {
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
