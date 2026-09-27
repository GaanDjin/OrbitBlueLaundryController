/*
D4   — RFID RST
D5   — RFID SS
D12  — HSPI MISO  (Touch TD0 + RFID MISO shared)
D13  — HSPI MOSI  (Touch TDI + RFID MOSI shared)
D14  — HSPI SCK   (Touch TCK + RFID SCK shared)
D15  — MACHINE_BUSY (washer units)
D18  — TFT SCK    (dryer units)
D21  — TFT DC     (dryer units)
D22  — TFT RST    (dryer units)
D23  — TFT SDI    (dryer units)
D25  — TOUCH IRQ  (dryer units)
D26  — RELAY
D27  — TFT CS     (dryer units)
D32  — TFT BL     (dryer units)
D33  — TOUCH CS   (dryer units)
D2   — LED RED    (washer units)
D19  — reserved for later
*/

#pragma once

#include <Config.h>

// ── Shared SPI bus (HSPI) — Touch + RFID ─────────────────────────────────────
#define SPI2_SCK  14
#define SPI2_MISO 12
#define SPI2_MOSI 13

// ── RFID ──────────────────────────────────────────────────────────────────────
#define RFID_SS  5
#define RFID_RST 4

// ── TFT display ───────────────────────────────────────────────────────────────
#if defined(HAS_DISPLAY)
  #define TFT_CS  27
  #define TFT_DC  21
  #define TFT_RST 22
  #define TFT_BL  32   // PWM backlight — GPIO32
  #define BL_BRIGHT 255
  #define BL_DIM      32
#endif

// ── Touch ─────────────────────────────────────────────────────────────────────
#if defined(HAS_TOUCH)
  #define TOUCH_CS  33
  #define TOUCH_IRQ 25
#endif

// ── LEDs ──────────────────────────────────────────────────────────────────────
#if defined(HAS_LED_GREEN)
  #define LED_GREEN_PIN 19   // adjust to your wiring
#endif

#if defined(HAS_LED_RED)
  #define LED_RED_PIN 2     // adjust to your wiring
#endif

// ── Machine IO ────────────────────────────────────────────────────────────────
#define RELAY1 26

#if defined(HAS_MACHINE_BUSY)
  #define MACHINE_BUSY 15
#endif

