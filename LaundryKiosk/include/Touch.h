#pragma once

#include <Config.h>
#include <Pins.h>

#if defined(HAS_TOUCH)

#include <Arduino.h>
#include <XPT2046_Touchscreen.h>

// ── Touch objects ─────────────────────────────────────────────────────────────
// Declared here, defined once in Touch.cpp
extern SPIClass*           touchSPI;
extern XPT2046_Touchscreen* touch;

// ── Setup ─────────────────────────────────────────────────────────────────────
void setupTouch();

// ── Poll ──────────────────────────────────────────────────────────────────────
// Returns true if the screen is being touched this loop pass.
// Uses tirqTouched() as a fast IRQ pre-check to avoid unnecessary SPI reads.
bool isTouched();

#endif // HAS_TOUCH