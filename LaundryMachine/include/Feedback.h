#pragma once

#include <Config.h>
#include <Pins.h>
#include <Arduino.h>

// ── Setup ─────────────────────────────────────────────────────────────────────
// Call once in setup() to configure any LED pins
void setupFeedback();

// ── Tap outcomes ──────────────────────────────────────────────────────────────
// Each function triggers the appropriate display message and/or LED pattern
// based on which hardware features are compiled in.

// Card accepted, funds deducted, machine starting
void tapSuccess(float newBalance);

// Card accepted, accumulation in progress — shows running time + cost
void tapAccumulating(int accumulatedSeconds, float accumulatedAmount);

// Accumulation capped — card balance can't cover another increment
void tapAccumMax(float balance);

// Not enough funds
void tapNSF(float balance, float cost);

// Card UID not found in the system
void tapCardNotFound();

// Machine is currently busy / in use
void tapMachineBusy();

// Deduct API call failed (network / server error)
void tapDeductFailed();