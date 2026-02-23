#include "wifi_manager.h"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <lwip/dns.h>
#include <lwip/napt.h>

#include "config_storage.h"

static unsigned long last_check = 0;
static const unsigned long CHECK_INTERVAL = 10000;

void wifi_manager_init(const char *sta_ssid, const char *sta_pass,
                       const char *ap_ssid, const char *ap_pass) {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(global_config.ap_ssid, global_config.ap_pass);

#ifdef DEBUG
  Serial.print("AP Started! SSID: ");
  Serial.println(global_config.ap_ssid);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
#endif

  WiFi.begin(sta_ssid, sta_pass);

#if LWIP_FEATURES && !LWIP_IPV6
  err_t ret = ip_napt_init(512, 32);
  if (ret == ERR_OK) {
    ip_napt_enable_no(SOFTAP_IF, 1);
#ifdef DEBUG
    Serial.println("[WiFi] NAT (Routing) Enabled!");
#endif
  }
#else
#error                                                                         \
    "Mbah Dukun bilang: LwIP NAT belum aktif! Ganti menu 'Tools -> LwIP Variant' di Arduino IDE ke 'v2 Higher Bandwidth (no features)' yang ada NAT!"
#endif

#ifdef DEBUG
  Serial.print("[WiFi] Connecting to upstream STA: ");
  Serial.println(sta_ssid);
#endif
}

void wifi_manager_loop() {
  unsigned long current = millis();

  if (current - last_check >= CHECK_INTERVAL) {
    last_check = current;

    if (WiFi.status() != WL_CONNECTED) {
#ifdef DEBUG
      Serial.println("[WiFi] Warning: Upstream WiFi disconnected. "
                     "Auto-reconnect is handling it...");
#endif
    }
  }
}

bool wifi_manager_is_connected() { return WiFi.status() == WL_CONNECTED; }
