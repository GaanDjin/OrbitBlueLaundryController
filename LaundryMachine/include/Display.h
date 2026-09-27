#pragma once

#include <Config.h>
#include <Pins.h>

#if defined(HAS_DISPLAY)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ── Color definitions ─────────────────────────────────────────────────────────
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

// ── GFX object ────────────────────────────────────────────────────────────────
// Declared here, defined once in Display.cpp
extern Arduino_DataBus* bus;
extern Arduino_GFX*     gfx;

// ── Balance bar state (used by Display.cpp and main.cpp) ─────────────────────
extern unsigned long balanceDisplayTime;
extern String        balanceDisplayStr;

// ── Setup ─────────────────────────────────────────────────────────────────────
void setupDisplay();
void setBacklight(bool active);   // full bright or dim based on activity
void setBrightness(uint8_t val);  // raw PWM value 0-255

// ── Persistent UI elements ────────────────────────────────────────────────────
void DrawHeader();
void ClearHeader();
void DrawTimer();
void DrawProgressBar();
void DrawBackground();

// ── Balance bar ───────────────────────────────────────────────────────────────
void showBalanceBar(const String& text, uint16_t color = GREEN);
void clearBalanceBar();
void showAccumDisplay(int accumulatedSeconds, float accumulatedAmount);

// ── Full screen messages ──────────────────────────────────────────────────────
void showReady();
void showTimesUp();
void showConnecting();
void showConnected();
void showLoggingIn();
void showLoggedIn();
void showRebooting();
void showProvisioning(const String& apName);
void showOutOfOrder();

// ── Helpers ───────────────────────────────────────────────────────────────────
uint16_t fadeRed(uint8_t level);
void     printText(char text[]);

#endif // HAS_DISPLAY