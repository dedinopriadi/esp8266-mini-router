#include "session_manager.h"

#include <Arduino.h>

extern "C" {
#include <user_interface.h>
}

#include "config_storage.h"

static ClientSession sessions[MAX_SESSIONS];
static const char ADMIN_VOUCHER_LABEL[] = "ADMIN";

static void copy_voucher_code(char *dst, const char *src) {
  if (dst == nullptr) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, sizeof(sessions[0].voucher_code) - 1);
  dst[sizeof(sessions[0].voucher_code) - 1] = '\0';
}

static bool release_voucher_code(const char *voucher_code) {
  if (voucher_code == nullptr || voucher_code[0] == '\0' ||
      strcmp(voucher_code, ADMIN_VOUCHER_LABEL) == 0) {
    return false;
  }

  for (int i = 0; i < VOUCHER_COUNT; i++) {
    if (strcmp(global_config.vouchers[i].code, voucher_code) == 0 &&
        global_config.vouchers[i].is_used) {
      global_config.vouchers[i].is_used = false;
      return true;
    }
  }
  return false;
}

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
  bool voucher_state_changed = false;

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].active) {
      if (current - sessions[i].timestamp > SESSION_TIMEOUT_MS) {
#ifdef DEBUG
        IPAddress expired_ip = sessions[i].ip;
#endif
        if (release_voucher_code(sessions[i].voucher_code)) {
          voucher_state_changed = true;
        }
        sessions[i].active = false;
        sessions[i].has_mac = false;
        sessions[i].ip = IPAddress(0, 0, 0, 0);
        sessions[i].voucher_code[0] = '\0';
#ifdef DEBUG
        Serial.print("[Session] Timeout! Evicted IP: ");
        Serial.printf("%d.%d.%d.%d\n", expired_ip[0], expired_ip[1],
                      expired_ip[2], expired_ip[3]);
#endif
      }
    }
  }

  if (voucher_state_changed) {
    config_storage_save();
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
    copy_voucher_code(used_voucher, ADMIN_VOUCHER_LABEL);
  } else {
    for (int i = 0; i < VOUCHER_COUNT; i++) {
      if (strlen(global_config.vouchers[i].code) > 0 &&
          !global_config.vouchers[i].is_used &&
          strcmp(password, global_config.vouchers[i].code) == 0) {
        is_valid = true;
        global_config.vouchers[i].is_used = true;
        copy_voucher_code(used_voucher, global_config.vouchers[i].code);
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
      bool voucher_state_changed = false;
      if (strcmp(sessions[i].voucher_code, used_voucher) != 0 &&
          release_voucher_code(sessions[i].voucher_code)) {
        voucher_state_changed = true;
      }
      sessions[i].timestamp = millis();
      copy_voucher_code(sessions[i].voucher_code, used_voucher);
      if (voucher_state_changed) {
        config_storage_save();
      }
      return true;
    }
  }

  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].active) {
      sessions[i].ip = ip;
      sessions[i].timestamp = millis();
      sessions[i].active = true;
      copy_voucher_code(sessions[i].voucher_code, used_voucher);
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
#ifdef DEBUG
      IPAddress logout_ip = sessions[i].ip;
#endif
      bool voucher_state_changed = release_voucher_code(sessions[i].voucher_code);
      sessions[i].active = false;
      sessions[i].has_mac = false;
      sessions[i].ip = IPAddress(0, 0, 0, 0);
      sessions[i].voucher_code[0] = '\0';
      if (voucher_state_changed) {
        config_storage_save();
      }
#ifdef DEBUG
      Serial.print("[Session] Explicit logout IP: ");
      Serial.printf("%d.%d.%d.%d\n", logout_ip[0], logout_ip[1], logout_ip[2],
                    logout_ip[3]);
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
