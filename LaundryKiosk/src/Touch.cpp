#include <Touch.h>

#if defined(HAS_TOUCH)

// ── Object definitions ────────────────────────────────────────────────────────
SPIClass*            touchSPI = new SPIClass(HSPI);
XPT2046_Touchscreen* touch    = new XPT2046_Touchscreen(TOUCH_CS, TOUCH_IRQ);

// ── Setup ─────────────────────────────────────────────────────────────────────

void setupTouch() {
    // Explicit pin mapping — HSPI defaults would conflict with TOUCH_IRQ on D14
    touchSPI->begin(SPI2_SCK, SPI2_MISO, SPI2_MOSI);

    if (!touch->begin(*touchSPI)) {
        Serial.println("[Touch] Init failed!");
    } else {
        Serial.println("[Touch] Ready.");
    }

    touch->setRotation(3);
}

// ── Poll ──────────────────────────────────────────────────────────────────────

bool isTouched() {
    return touch->tirqTouched() && touch->touched();
}

#endif // HAS_TOUCH