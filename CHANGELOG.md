# Changelog

## [1.1.0] - 2026-04-07

### Highlights
- Admin Panel has a cleaner and more professional look.
- Menu navigation is clearer and faster across all main sections.
- Dashboard now shows key router status more clearly.

### Improved Experience
- Web admin rendering is more stable in normal operating conditions.

### Safety
- Additional safeguards were added for critical actions to prevent unintended changes.

### Known Issue
- In low-memory conditions, the Admin Panel may load partially and some sections may not render completely.
- Why this happens: the ESP8266 has very limited RAM, and when memory is heavily used by WiFi/session/runtime tasks, less memory remains for full page generation and response delivery.

## [1.0.0] - 2026-02-23

### What's New
- Initial public release of ESP8266 Mini Router.
- Captive portal login with voucher-based access.
- WiFi repeater operation with AP+STA mode.
- Built-in admin panel for monitoring and configuration.
- OTA update support.
- Hardware long-press factory reset support.
