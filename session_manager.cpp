#include "session_manager.h"

#include <Arduino.h>

extern "C" {
#include <user_interface.h>
}

#include "config_storage.h"

static ClientSession sessions[MAX_SESSIONS];

static bool get_mac_from_ip(IPAddress ip, uint8_t *mac) {
  struct station_info *stat_info = wifi_softap_get_station_info();
  bool found = false;

  while (stat_info != NULL) {
    if (IPAddress(stat_info->ip.addr) == ip) {
      memcpy(mac, stat_info->bssid, 6);
      found = true;
      break;
    }
    stat_info = STAILQ_NEXT(stat_info, next);
  }

  wifi_softap_free_station_info();
  return found;
}

ClientSession *session_manager_get_sessions() { return sessions; }

void session_manager_init() {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    sessions[i].active = false;
    sessions[i].has_mac = false;
    sessions[i].ip = IPAddress(0, 0, 0, 0);
    sessions[i].timestamp = 0;
    sessions[i].voucher_code[0] = '\0';
  }
#ifdef DEBUG
  Serial.println("[Session] Manager initialized with max 5 clients.");
#endif
}

void session_manager_loop() {
  unsigned long current = millis();

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active) {
      if (current - sessions[i].timestamp > SESSION_TIMEOUT_MS) {
        sessions[i].active = false;
        sessions[i].voucher_code[0] = '\0';
#ifdef DEBUG
        Serial.print("[Session] Timeout! Evicted IP: ");
        Serial.printf("%d.%d.%d.%d\n", sessions[i].ip[0], sessions[i].ip[1],
                      sessions[i].ip[2], sessions[i].ip[3]);
#endif
      }
    }
  }
}

bool session_manager_is_authenticated(IPAddress ip) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active && sessions[i].ip == ip) {
      if (sessions[i].has_mac) {
        uint8_t current_mac[6];
        if (get_mac_from_ip(ip, current_mac)) {
          if (memcmp(current_mac, sessions[i].mac, 6) == 0) {
            return true;
          }
        }
        return false;
      }
      return true;
    }
  }
  return false;
}

unsigned long session_manager_get_remaining_time(IPAddress ip) {
  unsigned long current = millis();

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active && sessions[i].ip == ip) {
      unsigned long elapsed = current - sessions[i].timestamp;
      if (elapsed < SESSION_TIMEOUT_MS) {
        return SESSION_TIMEOUT_MS - elapsed;
      } else {
        return 0;
      }
    }
  }
  return 0;
}

bool session_manager_login(IPAddress ip, const char *password) {
  bool is_valid = false;
  char used_voucher[16] = "";

  if (strcmp(password, global_config.admin_pass) == 0) {
    is_valid = true;
    strcpy(used_voucher, "ADMIN");
  } else {
    for (int i = 0; i < 5; i++) {
      if (strlen(global_config.vouchers[i].code) > 0 &&
          !global_config.vouchers[i].is_used &&
          strcmp(password, global_config.vouchers[i].code) == 0) {
        is_valid = true;
        global_config.vouchers[i].is_used = true;
        strcpy(used_voucher, global_config.vouchers[i].code);
        config_storage_save();
        break;
      }
    }
  }

  if (!is_valid) {
#ifdef DEBUG
    Serial.println(
        "[Session] Failed login: Incorrect password or used/invalid voucher.");
#endif
    return false;
  }

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active && sessions[i].ip == ip) {
      sessions[i].timestamp = millis();
      strcpy(sessions[i].voucher_code, used_voucher);
      return true;
    }
  }

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].active) {
      sessions[i].ip = ip;
      sessions[i].timestamp = millis();
      sessions[i].active = true;
      strcpy(sessions[i].voucher_code, used_voucher);
      sessions[i].has_mac = get_mac_from_ip(ip, sessions[i].mac);

#ifdef DEBUG
      Serial.print("[Session] Login successful! New session IP: ");
      Serial.printf("%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
      if (sessions[i].has_mac) {
        Serial.printf("          MAC Bound: %02x:%02x:%02x:%02x:%02x:%02x\n",
                      sessions[i].mac[0], sessions[i].mac[1],
                      sessions[i].mac[2], sessions[i].mac[3],
                      sessions[i].mac[4], sessions[i].mac[5]);
      }
#endif
      return true;
    }
  }

#ifdef DEBUG
  Serial.println("[Session] Login failed: Session table is full.");
#endif
  return false;
}

void session_manager_logout(IPAddress ip) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active && sessions[i].ip == ip) {
      sessions[i].active = false;
#ifdef DEBUG
      Serial.print("[Session] Explicit logout IP: ");
      Serial.printf("%d.%d.%d.%d\n", sessions[i].ip[0], sessions[i].ip[1],
                    sessions[i].ip[2], sessions[i].ip[3]);
#endif
      return;
    }
  }
}

int session_manager_get_active_count() {
  int count = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active) {
      count++;
    }
  }
  return count;
}
