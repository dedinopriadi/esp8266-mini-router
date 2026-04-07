#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

void wifi_manager_init(const char *sta_ssid, const char *sta_pass);
void wifi_manager_loop();

bool wifi_manager_is_connected();

#endif
