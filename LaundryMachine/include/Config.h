#pragma once

// ── Firmware identity ─────────────────────────────────────────────────────────
// FIRMWARE_VERSION and FIRMWARE_PROFILE are set per environment in platformio.ini
// #ifndef guards here are fallbacks for any environment that doesn't set them.
#ifndef FIRMWARE_VERSION
  #define FIRMWARE_VERSION  "1.1.0"
#endif
#ifndef FIRMWARE_PROFILE
  #define FIRMWARE_PROFILE  "dryer"
#endif

// ── API host / port ───────────────────────────────────────────────────────────
// Host shown as default in the provisioning form.
// Can be overridden per environment in platformio.ini if needed.
#define API_HOST "blueorbit.localdomain"
#define API_PORT 443

// ── DNS test host (used during boot to verify connectivity) ───────────────────
#define testInitDnsHost "google.ca"

// ── Provisioning ──────────────────────────────────────────────────────────────
// Uncomment to wipe provisioning data on next boot (then re-comment + reflash).
#define CLEAR_PROVISION

// ── API client saved token ────────────────────────────────────────────────────
// Uncomment to clear saved JWT on next boot (then re-comment + reflash).
//#define CLEAR_CLIENT_PREFS

// ── Hardware feature flags ────────────────────────────────────────────────────
// ALL feature flags are set in platformio.ini build_flags — NOT here.
// Defining them here AND in build_flags causes redefinition warnings.
// HAS_DISPLAY, HAS_TOUCH, HAS_LED_GREEN, HAS_LED_RED,
// HAS_MACHINE_BUSY, RELAY_ON_START are all set per environment.

// ── Timing constants ──────────────────────────────────────────────────────────
#define HEARTBEAT_INTERVAL_MS    30000UL
#define HEARTBEAT_TIMEOUT_MS    600000UL
#define TAP_COOLDOWN_MS           1000UL
#define WAKEUP_COOLDOWN_MS       10000UL //How long between resetting the RFID reader.
#define ACCUM_COMMIT_OFFSET_MS     200UL
#define BALANCE_DISPLAY_MS       10000UL

// ── Relay polarity ────────────────────────────────────────────────────────────
// Set per environment in platformio.ini. Default here is fallback only.
#ifndef RELAY_ACTIVE
  #define RELAY_ACTIVE true
#endif

// ── Machine busy polarity ─────────────────────────────────────────────────────
// Set per environment in platformio.ini. Default here is fallback only.
#ifndef MACHINE_BUSY_ACTIVE_HIGH
  #define MACHINE_BUSY_ACTIVE_HIGH false
#endif
