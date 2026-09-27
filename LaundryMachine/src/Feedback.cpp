#include <Feedback.h>

#if defined(HAS_DISPLAY)
  #include <Display.h>
#endif

// ── LED helper ────────────────────────────────────────────────────────────────

#if defined(HAS_LED_GREEN) || defined(HAS_LED_RED)

static void flashLED(int pin, int flashes, int onMs, int offMs) {
    for (int i = 0; i < flashes; i++) {
        digitalWrite(pin, HIGH);
        delay(onMs);
        digitalWrite(pin, LOW);
        if (i < flashes - 1) delay(offMs);
    }
}

#endif

// ── Setup ─────────────────────────────────────────────────────────────────────

void setupFeedback() {
#if defined(HAS_LED_GREEN)
    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LOW);
#endif

#if defined(HAS_LED_RED)
    pinMode(LED_RED_PIN, OUTPUT);
    digitalWrite(LED_RED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_RED_PIN, LOW);
#endif
}

// ── Tap outcomes ──────────────────────────────────────────────────────────────

void tapSuccess(float newBalance) {
#if defined(HAS_DISPLAY)
    showBalanceBar("Card OK  Bal: $" + String(newBalance, 2), GREEN);
#endif
#if defined(HAS_LED_GREEN)
    flashLED(LED_GREEN_PIN, 2, 150, 100);  // two quick green flashes
#endif
}

void tapAccumulating(int accumulatedSeconds, float accumulatedAmount) {
#if defined(HAS_DISPLAY)
    showAccumDisplay(accumulatedSeconds, accumulatedAmount);
#endif
#if defined(HAS_LED_GREEN)
    flashLED(LED_GREEN_PIN, 1, 80, 0);    // single brief green pulse per increment
#endif
}

void tapAccumMax(float balance) {
#if defined(HAS_DISPLAY)
    showBalanceBar("Max time reached. Bal: $" + String(balance, 2), YELLOW);
#endif
    // No LED — not an error, just a cap
}

void tapNSF(float balance, float cost) {
#if defined(HAS_DISPLAY)
    showBalanceBar("NSF. Cost: $" + String(cost, 2) + "  Bal: $" + String(balance, 2), RED);
#endif
#if defined(HAS_LED_RED)
    flashLED(LED_RED_PIN, 3, 400, 200);   // three slow red flashes
#endif
}

void tapCardNotFound() {
#if defined(HAS_DISPLAY)
    showBalanceBar("Card not found.", RED);
#endif
#if defined(HAS_LED_RED)
    flashLED(LED_RED_PIN, 1, 1000, 0);    // one long red flash
#endif
}

void tapMachineBusy() {
#if defined(HAS_DISPLAY)
    showBalanceBar("Machine busy.", RED);
#endif
#if defined(HAS_LED_RED)
    flashLED(LED_RED_PIN, 2, 200, 200);   // two medium red flashes
#endif
}

void tapDeductFailed() {
#if defined(HAS_DISPLAY)
    showBalanceBar("Deduct failed. Try again.", RED);
#endif
#if defined(HAS_LED_RED)
    flashLED(LED_RED_PIN, 3, 200, 100);
#endif
}
