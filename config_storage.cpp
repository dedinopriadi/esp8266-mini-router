#include "config_storage.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

RouterConfig global_config;
static const char *CONFIG_FILE = "/config.bin";

void config_storage_init() {
  if (!LittleFS.begin()) {
#ifdef DEBUG
    Serial.println("[LittleFS] Format parsing failed. Formatting...");
#endif
    LittleFS.format();
    if (!LittleFS.begin()) {
#ifdef DEBUG
      Serial.println("[LittleFS] Critical Error: Unable to mount LittleFS.");
#endif
      strncpy(global_config.sta_ssid, "Dedi Nopriadi",
              sizeof(global_config.sta_ssid) - 1);
      strncpy(global_config.sta_pass, "23111998",
              sizeof(global_config.sta_pass) - 1);
      strncpy(global_config.admin_user, "admin",
              sizeof(global_config.admin_user) - 1);
      strncpy(global_config.admin_pass, "admin",
              sizeof(global_config.admin_pass) - 1);
      strncpy(global_config.ap_ip, "192.168.4.1",
              sizeof(global_config.ap_ip) - 1);
      return;
    }
  }

  if (LittleFS.exists(CONFIG_FILE)) {
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (f) {
      memset(&global_config, 0, sizeof(RouterConfig));
      f.read((uint8_t *)&global_config, sizeof(RouterConfig));
      f.close();

#ifdef DEBUG
      Serial.println("[LittleFS] Config loaded successfully.");
#endif

      global_config.sta_ssid[sizeof(global_config.sta_ssid) - 1] = '\0';
      global_config.sta_pass[sizeof(global_config.sta_pass) - 1] = '\0';
      global_config.ap_ssid[sizeof(global_config.ap_ssid) - 1] = '\0';
      global_config.ap_pass[sizeof(global_config.ap_pass) - 1] = '\0';
      global_config.admin_user[sizeof(global_config.admin_user) - 1] = '\0';
      global_config.admin_pass[sizeof(global_config.admin_pass) - 1] = '\0';
      global_config.ap_ip[sizeof(global_config.ap_ip) - 1] = '\0';

      for (int i = 0; i < VOUCHER_COUNT; i++) {
        global_config.vouchers[i].code[15] = '\0';
      }
      return;
    }
  }

#ifdef DEBUG
  Serial.println("[LittleFS] Using default compiled config.");
#endif

  memset(&global_config, 0, sizeof(RouterConfig));
  strncpy(global_config.sta_ssid, "Upstream_SSID",
          sizeof(global_config.sta_ssid) - 1);
  strncpy(global_config.sta_pass, "Upstream_PASS",
          sizeof(global_config.sta_pass) - 1);
  strncpy(global_config.ap_ssid, "MiniRouter_AP",
          sizeof(global_config.ap_ssid) - 1);
  strncpy(global_config.ap_pass, "12345678", sizeof(global_config.ap_pass) - 1);
  strncpy(global_config.admin_user, "admin",
          sizeof(global_config.admin_user) - 1);
  strncpy(global_config.admin_pass, "admin",
          sizeof(global_config.admin_pass) - 1);
  strncpy(global_config.ap_ip, "192.168.4.1", sizeof(global_config.ap_ip) - 1);

  randomSeed(micros());
  for (int i = 0; i < VOUCHER_COUNT; i++) {
    config_storage_generate_random(global_config.vouchers[i].code, 6);
    global_config.vouchers[i].is_used = false;
  }
  config_storage_save();
}

void config_storage_generate_random(char *str, int length) {
  const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

  for (int i = 0; i < length; i++) {
    int key = random(0, sizeof(charset) - 1);
    str[i] = charset[key];
  }
  str[length] = '\0';
}

void config_storage_save() {
  File f = LittleFS.open(CONFIG_FILE, "w");

  if (f) {
    f.write((const uint8_t *)&global_config, sizeof(RouterConfig));
    f.close();
#ifdef DEBUG
    Serial.println("[LittleFS] Config saved to flash.");
#endif
  } else {
#ifdef DEBUG
    Serial.println("[LittleFS] Error opening file for writing.");
#endif
  }
}

void config_storage_factory_reset() {
#ifdef DEBUG
  Serial.println("FACTORY RESET: Erasing config file...");
#endif

  if (LittleFS.begin()) {
    LittleFS.remove(CONFIG_FILE);
  }

#ifdef DEBUG
  Serial.println("FACTORY RESET: Complete. Rebooting in 1s...");
#endif

  delay(1000);
  ESP.restart();
}
