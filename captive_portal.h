#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

void captive_portal_init();
void captive_portal_start();
void captive_portal_loop();

AsyncWebServer *get_web_server();

#endif
