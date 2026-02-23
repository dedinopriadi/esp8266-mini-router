#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Arduino.h>

struct VoucherRecord {
  char code[16];
  bool is_used;
};

struct RouterConfig {
  char sta_ssid[32];
  char sta_pass[64];
  char ap_ssid[32];
  char ap_pass[64];
  char admin_pass[32];
  VoucherRecord vouchers[5];
};

extern RouterConfig global_config;

void config_storage_init();
void config_storage_save();
void config_storage_factory_reset();
void config_storage_generate_random(char *str, int length);

#endif
