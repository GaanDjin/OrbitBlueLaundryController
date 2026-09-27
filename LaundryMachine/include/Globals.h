#pragma once

#include <Arduino.h>
#include <Config.h>
#include <Pins.h>
#include <ApiClient.h>

// ── Ealth Checks ────────────────────────────────────────────────────────────────────
extern unsigned long lastWifiCheck;
extern unsigned long lastWifiConnected;
extern unsigned long lastSuccessfulHeartbeat;

// ── API client ────────────────────────────────────────────────────────────────
extern ApiClient* api;
static unsigned long lastConfigFetch = 0;
const unsigned long CONFIG_FETCH_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;

// ── Timing ────────────────────────────────────────────────────────────────────
extern unsigned long lastScreenTime;
extern unsigned long lastTapTime;
extern unsigned long lastWakeupTime;
extern unsigned long lastHeartbeat;
extern unsigned long lastTick;
extern unsigned long lastFade;
extern unsigned long messageTime;
extern unsigned long lastPulse;
extern unsigned long countdownStartMs;
extern int           countdownTotalSeconds;

// ── Timer state ───────────────────────────────────────────────────────────────
extern unsigned long countdownSeconds;
extern unsigned long maxSeconds;
extern long          timesUpMessageDelay;
extern bool          runOnce;
extern bool          isOutOfOrder;
extern int           optimisticSeconds;  // countdown seconds added optimistically, rolled back on deduct failure

// ── Display state ─────────────────────────────────────────────────────────────
extern int fadeValue;
extern int fadeDir;

// ── Machine config (loaded from server, defaults set in Globals.cpp) ──────────
extern bool          timerMode;
extern bool          useMachineBusy;
extern bool          coinMode;
extern int           coinCount;
extern int           coinPulseDuration;
extern int           coinPulseDelay;
extern int           busyCooldownSeconds;
extern unsigned long cycleLengthSeconds;
extern double        Amount;
extern int           screenTimeout;



// false = active LOW (Machine is busy when pin is LOW)
// true =  active HIGH (machine is busy when pin is HIGH)
#define MACHINE_BUSY_HIGH false
