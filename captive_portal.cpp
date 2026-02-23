#include "captive_portal.h"

#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <WiFiUdp.h>

#include "html_templates.h"
#include "session_manager.h"

static const byte DNS_PORT = 53;
static WiFiUDP dnsUDP;
static WiFiUDP proxyUDP;

AsyncWebServer server(80);

struct DNSQueryState {
  bool active = false;
  uint16_t txId;
  IPAddress clientIP;
  uint16_t clientPort;
  unsigned long timestamp;
};

static DNSQueryState pendingQueries[5];

void captive_portal_init() {
  proxyUDP.begin(10053);
  dnsUDP.begin(DNS_PORT);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    IPAddress clientIP = request->client()->remoteIP();

    if (session_manager_is_authenticated(clientIP)) {
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->print(FPSTR(success_header_html));

      ClientSession *sessions = session_manager_get_sessions();
      char voucherStr[16] = "Unknown";
      for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && sessions[i].ip == clientIP) {
          strncpy(voucherStr, sessions[i].voucher_code, 15);
          voucherStr[15] = '\0';
          break;
        }
      }

      response->printf("%s", voucherStr);
      response->print(FPSTR(success_middle_html));

      unsigned long remaining = session_manager_get_remaining_time(clientIP);
      unsigned long rh = remaining / 3600000;
      remaining %= 3600000;
      unsigned long rm = remaining / 60000;
      unsigned long rs = (remaining % 60000) / 1000;

      response->printf("%02lu:%02lu:%02lu", rh, rm, rs);
      response->print(FPSTR(success_footer_html));
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse_P(200, "text/html", login_html);
      response->addHeader("Cache-Control",
                          "no-cache, no-store, must-revalidate");
      request->send(response);
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    IPAddress clientIP = request->client()->remoteIP();

    if (session_manager_is_authenticated(clientIP)) {
      AsyncResponseStream *response = request->beginResponseStream("text/html");
      response->print(FPSTR(success_header_html));

      ClientSession *sessions = session_manager_get_sessions();
      char voucherStr[16] = "Unknown";
      for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && sessions[i].ip == clientIP) {
          strncpy(voucherStr, sessions[i].voucher_code, 15);
          voucherStr[15] = '\0';
          break;
        }
      }

      response->printf("%s", voucherStr);
      response->print(FPSTR(success_middle_html));

      unsigned long remaining = session_manager_get_remaining_time(clientIP);
      unsigned long rh = remaining / 3600000;
      remaining %= 3600000;
      unsigned long rm = remaining / 60000;
      unsigned long rs = (remaining % 60000) / 1000;

      response->printf("%02lu:%02lu:%02lu", rh, rm, rs);
      response->print(FPSTR(success_footer_html));
      request->send(response);
    } else {
      AsyncWebServerResponse *response =
          request->beginResponse_P(200, "text/html", login_html);
      response->addHeader("Cache-Control",
                          "no-cache, no-store, must-revalidate");
      request->send(response);
    }
  });

  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("password", true)) {
      const char *pwd = request->getParam("password", true)->value().c_str();
      IPAddress clientIP = request->client()->remoteIP();

      if (session_manager_login(clientIP, pwd)) {
        request->redirect("/");
      } else {
        request->send(
            401, "text/html",
            "<html><body><h2>Login Failed</h2><p>Incorrect password or "
            "session full.</p><br><a href='/'>Try Again</a></body></html>");
      }
    } else {
      request->send(400, "text/plain", "Missing password parameter");
    }
  });
}

AsyncWebServer *get_web_server() { return &server; }

void captive_portal_start() {
  server.begin();
#ifdef DEBUG
  Serial.println("[Captive] Web Server & Smart DNS Proxy started.");
#endif
}

void captive_portal_loop() {
  unsigned long current = millis();

  int packetSize = dnsUDP.parsePacket();
  if (packetSize > 0) {
    IPAddress remoteIP = dnsUDP.remoteIP();
    uint16_t remotePort = dnsUDP.remotePort();
    uint8_t buf[512];
    int len = dnsUDP.read(buf, 512);

    if (!session_manager_is_authenticated(remoteIP)) {
      if (len >= 12 && len <= 512 - 16) {
        buf[2] |= 0x80;
        buf[3] |= 0x80;
        buf[7] = 1;
        IPAddress apIP = WiFi.softAPIP();
        uint8_t ans[16] = {0xC0,    0x0C,    0x00,    0x01,   0x00, 0x01,
                           0x00,    0x00,    0x00,    0x3C,   0x00, 0x04,
                           apIP[0], apIP[1], apIP[2], apIP[3]};
        dnsUDP.beginPacket(remoteIP, remotePort);
        dnsUDP.write(buf, len);
        dnsUDP.write(ans, 16);
        dnsUDP.endPacket();
      }
    } else {
      if (len >= 2) {
        uint16_t txId = (buf[0] << 8) | buf[1];
        for (int i = 0; i < 5; i++) {
          if (!pendingQueries[i].active) {
            pendingQueries[i].active = true;
            pendingQueries[i].txId = txId;
            pendingQueries[i].clientIP = remoteIP;
            pendingQueries[i].clientPort = remotePort;
            pendingQueries[i].timestamp = current;

            proxyUDP.beginPacket(IPAddress(8, 8, 8, 8), 53);
            proxyUDP.write(buf, len);
            proxyUDP.endPacket();
            break;
          }
        }
      }
    }
  }

  int proxySize = proxyUDP.parsePacket();
  if (proxySize > 0) {
    uint8_t buf[512];
    int len = proxyUDP.read(buf, 512);

    if (len >= 2) {
      uint16_t txId = (buf[0] << 8) | buf[1];
      for (int i = 0; i < 5; i++) {
        if (pendingQueries[i].active && pendingQueries[i].txId == txId) {
          dnsUDP.beginPacket(pendingQueries[i].clientIP,
                             pendingQueries[i].clientPort);
          dnsUDP.write(buf, len);
          dnsUDP.endPacket();
          pendingQueries[i].active = false;
          break;
        }
      }
    }
  }

  for (int i = 0; i < 5; i++) {
    if (pendingQueries[i].active &&
        (current - pendingQueries[i].timestamp > 3000)) {
      pendingQueries[i].active = false;
    }
  }
}
