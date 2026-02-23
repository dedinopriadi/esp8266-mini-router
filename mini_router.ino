#include <Arduino.h>

#include "admin_panel.h"
#include "captive_portal.h"
#include "config_storage.h"
#include "session_manager.h"
#include "wifi_manager.h"

#define PIN_LED LED_BUILTIN
#define PIN_BTN 0
#define DEBUG

const char *AP_SSID = "Mini_Hotspot";
const char *AP_PASS = "12345678";

unsigned long last_led_blink = 0;
bool led_state = false;
unsigned long btn_pressed_time = 0;
bool btn_is_pressing = false;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(100);
  Serial.println("\n--- Starting ESP8266 Mini Router ---");
#endif

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_BTN, INPUT_PULLUP);

  config_storage_init();
  session_manager_init();
  wifi_manager_init(global_config.sta_ssid, global_config.sta_pass, AP_SSID,
                    AP_PASS);
  captive_portal_init();
  admin_panel_init();
  captive_portal_start();

#ifdef DEBUG
  Serial.print("Initial Free Heap: ");
  Serial.println(ESP.getFreeHeap());
#endif
}

void loop() {
  wifi_manager_loop();
  captive_portal_loop();
  session_manager_loop();

  unsigned long current_time = millis();

  if (wifi_manager_is_connected()) {
    if (digitalRead(PIN_LED) == HIGH) {
      digitalWrite(PIN_LED, LOW);
    }
  } else {
    if (current_time - last_led_blink >= 500) {
      last_led_blink = current_time;
      led_state = !led_state;
      digitalWrite(PIN_LED, led_state ? LOW : HIGH);
    }
  }

  if (digitalRead(PIN_BTN) == LOW) {
    if (!btn_is_pressing) {
      btn_is_pressing = true;
      btn_pressed_time = current_time;
    } else {
      if (current_time - btn_pressed_time >= 5000) {
        digitalWrite(PIN_LED, HIGH);
        config_storage_factory_reset();
      }
    }
  } else {
    btn_is_pressing = false;
  }
}
