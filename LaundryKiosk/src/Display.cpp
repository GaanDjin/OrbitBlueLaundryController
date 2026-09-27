#include <Display.h>

#if defined(HAS_DISPLAY)

#include <Globals.h>

// ── GFX object definitions ────────────────────────────────────────────────────
Arduino_DataBus* bus = new Arduino_HWSPI(TFT_DC, TFT_CS);
Arduino_GFX*     gfx = new Arduino_ILI9488_18bit(bus, TFT_RST, false);

// ── Balance bar state ─────────────────────────────────────────────────────────
unsigned long balanceDisplayTime = 0;
String        balanceDisplayStr  = "";

// ── Setup ─────────────────────────────────────────────────────────────────────

void setupDisplay() {
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);

    gfx->begin();
    gfx->setRotation(1);
    gfx->fillScreen(BLACK);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setTextSize(3);

    // PWM backlight — channel 0, 5000Hz, 8-bit
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, BL_BRIGHT);
}

void setBacklight(bool active) {
    ledcWrite(0, active ? BL_BRIGHT : BL_DIM);
}

void setBrightness(uint8_t val) {
    ledcWrite(0, val);
}

// ── Balance bar ───────────────────────────────────────────────────────────────

void showBalanceBar(const String& text, uint16_t color) {
    gfx->fillRect(0, 0, 480, 48, BLACK);
    gfx->setTextSize(3);
    gfx->setTextColor(color, BLACK);
    gfx->setCursor(10, 6);
    gfx->print(text);  // print not println — avoids leaving cursor at bottom
    balanceDisplayTime = millis();
    balanceDisplayStr  = text;
}

void clearBalanceBar() {
    gfx->fillRect(0, 0, 480, 46, BLACK);
    balanceDisplayTime = 0;
    balanceDisplayStr  = "";
}

void showAccumDisplay(int accumulatedSeconds, float accumulatedAmount) {
    int    minutes = accumulatedSeconds / 60;
    int    secs    = accumulatedSeconds % 60;
    String line    = String(minutes) + "m " + String(secs) + "s  -  $"
                     + String(accumulatedAmount, 2);
    showBalanceBar(line, YELLOW);
}

// ── Persistent UI elements ────────────────────────────────────────────────────

void DrawHeader() {
    gfx->setTextColor(WHITE, BLACK);
    gfx->setTextSize(3);
    gfx->setCursor(10, 50);  // pushed down to clear the balance bar at y=0-40
    gfx->println("Tap laundry card\n to add time");
}

void ClearHeader() {
    gfx->fillRect(0, 50, 480, 80, BLACK);
}
void DrawTimer() {
    char buf[16];
    int  minutes = countdownSeconds / 60;
    int  seconds = countdownSeconds % 60;
    sprintf(buf, "%02d:%02d", minutes, seconds);

    gfx->setTextSize(6);

    int16_t  x1, y1;
    uint16_t w, h;
    gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

    // Account for x1 offset in centering — this is what causes the shift
    int16_t drawX = (gfx->width()  - w) / 2 - x1;
    int16_t drawY = (gfx->height() - h) / 2 - y1;

    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(drawX, drawY);
    gfx->print(buf);
}

void DrawProgressBar() {
    const int barX = 40;
    const int barY = 220;
    const int barW = 400;
    const int barH = 20;

    int fillW = 0;
    if (maxSeconds > 0)
        fillW = barW - (int)((countdownSeconds * barW) / maxSeconds);

    if (fillW > 0) {
        uint16_t color = GREEN;
        if      (countdownSeconds < 10) color = RED;
        else if (countdownSeconds < 30) color = YELLOW;
        gfx->fillRect(barX + 1, barY + 1, fillW - 2, barH - 2, color);
    }
}

void DrawBackground() {
    uint16_t bg = BLACK;

    if (countdownSeconds > 0 && countdownSeconds <= 10) {
        if (millis() - lastFade >= 30) {
            lastFade = millis();
            fadeValue += fadeDir;
            if (fadeValue >= 255 || fadeValue <= 0)
                fadeDir = -fadeDir;
        }
        bg = fadeRed(fadeValue);
    }

    gfx->fillScreen(bg);
}

// ── Full screen messages ──────────────────────────────────────────────────────

void showReady() {
    gfx->fillScreen(BLACK);
    gfx->setTextSize(8);
    gfx->setCursor(100, 160);
    gfx->setTextColor(GREEN, BLACK);
    gfx->print("Ready.");
    gfx->setTextSize(3);
    DrawHeader();
}

void showTimesUp() {
    gfx->fillScreen(BLACK);
    gfx->setTextSize(8);
    gfx->setCursor(80, 160);
    gfx->setTextColor(RED, BLACK);
    gfx->print("TIMES UP");
    gfx->setTextSize(3);
    DrawHeader();
}

void showConnecting() {
    gfx->fillRect(0, 0, 480, 40, BLACK);
    gfx->setCursor(10, 20);
    gfx->setTextSize(3);
    gfx->setTextColor(WHITE, BLACK);
    gfx->println("Connecting to WiFi...");
}

void showConnected() {
    gfx->setTextColor(GREEN, BLACK);
    gfx->println("Connected!");
}

void showLoggingIn() {
    gfx->setTextColor(WHITE, BLACK);
    gfx->println("Logging in...");
}

void showLoggedIn() {
    gfx->setTextColor(GREEN, BLACK);
    gfx->println("Logged in!");
}

void showRebooting() {
    showBalanceBar("Rebooting...", YELLOW);
}

void showProvisioning(const String& apName) {
    gfx->fillScreen(BLACK);

    // Title
    gfx->setTextSize(4);
    gfx->setTextColor(YELLOW, BLACK);
    gfx->setCursor(60, 20);
    gfx->print("SETUP MODE");

    // Divider
    gfx->drawFastHLine(20, 70, 440, YELLOW);

    // WiFi AP name
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(20, 90);
    gfx->print("Connect to WiFi:");
    gfx->setTextSize(3);
    gfx->setTextColor(CYAN, BLACK);
    gfx->setCursor(20, 115);
    gfx->print(apName);

    // Password hint
    gfx->setTextSize(2);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(20, 160);
    gfx->print("Password: orbitsetup");

    // URL
    gfx->drawFastHLine(20, 195, 440, 0x4208);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(20, 205);
    gfx->print("Then browse to:");
    gfx->setTextSize(3);
    gfx->setTextColor(CYAN, BLACK);
    gfx->setCursor(20, 230);
    gfx->print("http://192.168.4.1");

    // Footer
    gfx->setTextSize(2);
    gfx->setTextColor(0x4208, BLACK);
    gfx->setCursor(20, 290);
    gfx->print("See docs for help.");
}

void showOutOfOrder() {
    gfx->fillScreen(BLACK);

    // Big red X
    gfx->fillCircle(240, 130, 90, RED);
    gfx->setTextSize(10);
    gfx->setTextColor(WHITE, RED);
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds("X", 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(240 - w/2 - x1, 130 - h/2 - y1);
    gfx->print("X");

    // Text
    gfx->setTextSize(4);
    gfx->setTextColor(RED, BLACK);
    gfx->setCursor(60, 250);
    gfx->print("OUT OF ORDER");

    gfx->setTextSize(2);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(90, 300);
    gfx->print("Please use another machine");
}

// ── Helpers ───────────────────────────────────────────────────────────────────

uint16_t fadeRed(uint8_t level) {
    uint8_t red = map(level, 0, 255, 0, 31);
    return (red << 11);
}

void printText(char text[]) {
    gfx->fillRect(10, 10, 200, 20, BLACK);
    gfx->setCursor(10, 10);
    gfx->setTextColor(RGB565_RED);
    gfx->println(text);
}

#endif // HAS_DISPLAY