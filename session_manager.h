#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <Arduino.h>
#include <IPAddress.h>

#define MAX_SESSIONS 5
#define SESSION_TIMEOUT_MS 3600000

struct ClientSession {
  IPAddress ip;
  uint8_t mac[6];
  bool has_mac;
  unsigned long timestamp;
  bool active;
  char voucher_code[16];
};

void session_manager_init();
void session_manager_loop();
bool session_manager_is_authenticated(IPAddress ip);
unsigned long session_manager_get_remaining_time(IPAddress ip);
bool session_manager_login(IPAddress ip, const char *password);
void session_manager_logout(IPAddress ip);
int session_manager_get_active_count();

ClientSession *session_manager_get_sessions();

#endif
