#include <Arduino.h>

#include "admin_panel.h"
#include "captive_portal.h"
#include "config_storage.h"
#include "session_manager.h"
#include "version.h"
#include "wifi_manager.h"

#define PIN_LED LED_BUILTIN
#define PIN_BTN 0
#define DEBUG

static const unsigned long LED_BLINK_INTERVAL_MS = 500;
static const unsigned long FACTORY_RESET_HOLD_MS = 5000;

static unsigned long last_led_blink = 0;
static bool led_state = false;
static unsigned long btn_pressed_time = 0;
static bool btn_is_pressing = false;

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(100);
  Serial.print("\n--- Starting ESP8266 Mini Router (");
  Serial.print(FIRMWARE_VERSION);
  Serial.println(") ---");
#endif

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_BTN, INPUT_PULLUP);

  config_storage_init();

  session_manager_init();
  wifi_manager_init(global_config.sta_ssid, global_config.sta_pass);
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
    if (current_time - last_led_blink >= LED_BLINK_INTERVAL_MS) {
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
      if (current_time - btn_pressed_time >= FACTORY_RESET_HOLD_MS) {
        digitalWrite(PIN_LED, HIGH);
        config_storage_factory_reset();
      }
    }
  } else {
    btn_is_pressing = false;
  }
}
