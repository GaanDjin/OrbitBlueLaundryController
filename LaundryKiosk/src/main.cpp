#include <main.h>
#include "esp_system.h"

// ── RFID objects ──────────────────────────────────────────────────────────────

#if defined(HAS_TOUCH)
  // touchSPI is owned by Touch.cpp — RFID piggybacks on the same bus
  extern SPIClass* touchSPI;
#else
  // No touch — we own the HSPI bus here
  SPIClass* touchSPI = new SPIClass(HSPI);
#endif

MFRC522DriverPinSimple* ss_pin  = nullptr;
MFRC522DriverSPI*       driver  = nullptr;
MFRC522*                mfrc522 = nullptr;


// ── Provisioned credentials ───────────────────────────────────────────────────
ProvisionData provData;

// ── Accumulation state (unused in kiosk mode, but declared in main.h) ─────────
String        accumulatingCardId  = "";
float         accumulatedAmount   = 0.0f;
int           accumulatedSeconds  = 0;
float         accumulatedBalance  = 0.0f;
unsigned long lastAccumRead       = 0;
bool          isAccumulating      = false;

// ── State ─────────────────────────────────────────────────────────────────────
static unsigned long lastHeartbeatMs = 0;

// ─────────────────────────────────────────────────────────────────────────────
// RFID helpers
// ─────────────────────────────────────────────────────────────────────────────

void setupRFID() {
#if !defined(HAS_TOUCH)
    touchSPI->begin(SPI2_SCK, SPI2_MISO, SPI2_MOSI);
#endif
    // Now construct in the correct order, after SPI bus is up
    ss_pin  = new MFRC522DriverPinSimple(RFID_SS);
    driver  = new MFRC522DriverSPI{*ss_pin, *touchSPI};
    mfrc522 = new MFRC522{*driver};

    Serial.println("[RFID] Initialising...");
    if (!mfrc522->PCD_Init()) {
        Serial.println("[RFID] Init failed!");
    } else {
        MFRC522Debug::PCD_DumpVersionToSerial(*mfrc522, Serial);
        Serial.println("[RFID] Ready.");
    }
}

String printUid(const MFRC522::Uid uid) {
        String hex = "";
    for (size_t i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) hex += "0";
        hex += String(uid.uidByte[i], HEX);
    }
    hex.toUpperCase();
    return hex;
}

bool rfidHealthy() {
    bool ok = mfrc522 && mfrc522->PCD_PerformSelfTest();
    digitalWrite(RFID_SS, HIGH);
    touchSPI->endTransaction();
    return ok;
}

void resetRFIDModule() {
    if (mfrc522) mfrc522->PCD_Reset();
    delay(50);
    if (mfrc522) mfrc522->PCD_Init();
}

// Polls for a new card and reads its serial.
// Explicitly cleans up the SPI bus after every call so the shared HSPI bus
// is fully released before the XPT2046 touch controller gets its turn.
// Returns true if a card UID is ready in mfrc522->uid.
static bool rfidPoll() {
    bool cardPresent = mfrc522->PICC_IsNewCardPresent();
    // RFID_SS may still be asserted — force deassert and close any open transaction
    digitalWrite(RFID_SS, HIGH);
    touchSPI->endTransaction();

    if (!cardPresent) return false;

    bool gotSerial = mfrc522->PICC_ReadCardSerial();
    digitalWrite(RFID_SS, HIGH);
    touchSPI->endTransaction();

    return gotSerial;
}

// ─────────────────────────────────────────────────────────────────────────────
// Machine stubs (kiosk — no relay, no cycle)
// ─────────────────────────────────────────────────────────────────────────────

void  startMachineCycle()             { /* kiosk: not used */ }
void  addCycleTime(int /*seconds*/)   { /* kiosk: not used */ }
void  cancelCycle()                   { /* kiosk: not used */ }
bool  machineIsRunning()              { return false; }
void  commitAccumulation()            { /* kiosk: not used */ }
void  applyConfig(const String& /*j*/){ /* kiosk: not used */ }
void  fetchConfig()                   { /* kiosk: not used */ }

// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────

void connectWifi() {
    showConnecting();
    WiFi.mode(WIFI_STA);
    WiFi.begin(provData.wifiSsid.c_str(), provData.wifiPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 15000) {
            Serial.println("[WiFi] Timeout — rebooting.");
            ESP.restart();
        }
        delay(250);
    }
    showConnected();
    Serial.println("[WiFi] Connected: " + WiFi.localIP().toString());
}

// ─────────────────────────────────────────────────────────────────────────────
// Auth
// ─────────────────────────────────────────────────────────────────────────────


bool ensureAuthenticated() {
    // Wait for NTP to sync so time() is valid for expiry checks
    time_t now = time(nullptr);
    while (now < 1000000000UL) {
        delay(100);
        now = time(nullptr);
    }

    // Try to load cached credentials
    if (api->loadCredentials()) {
        if (api->needsRelogin()) {
            Serial.println("[Auth] Refresh token expired — full login.");
        } else if (api->needsRefresh()) {
            Serial.println("[Auth] Access token expiring — refreshing.");
            if (api->refreshToken()) return true;
            Serial.println("[Auth] Refresh failed — full login.");
        } else {
            // Verify the cached token actually works before trusting it
            // This catches wrong port/host after re-provisioning
            Serial.println("[Auth] Verifying cached token...");
            String response;
            if (api->get("/accounts/current", response, true)) {
                Serial.println("[Auth] Cached token valid.");
                return true;
            }
            Serial.println("[Auth] Cached token rejected — full login.");
            api->clearCredentials();
        }
    }

    // Full login
    Serial.println("[Auth] Logging in...");
    if (api->login(provData.apiUser, provData.apiPass)) {
        Serial.println("[Auth] Logged in!");
        if (!provData.verified) {
            markProvisionVerified();
            provData.verified = true;
        }
        return true;
    }

    Serial.println("[Auth] Login failed!");
    if (!provData.verified) {
        // Never successfully logged in — bad credentials, reprovision
        Serial.println("[Auth] Never verified — clearing provisioning.");
        clearProvisionData();
    } else {
        // Worked before — infrastructure problem, reboot and retry
        Serial.println("[Auth] Previously verified — rebooting to retry.");
    }
    #if defined(HAS_DISPLAY)
        showRebooting();
    #endif
    delay(1000);
    ESP.restart();
    return false;  // unreachable
}

// ─────────────────────────────────────────────────────────────────────────────
// Heartbeat  (always reports Idle for the kiosk)
// ─────────────────────────────────────────────────────────────────────────────

void doHeartbeat() {
    if (millis() - lastHeartbeat < HEARTBEAT_INTERVAL_MS) return;
    lastHeartbeat = millis();

    MachineStatus currentStatus = isOutOfOrder        ? MachineStatus::OutOfOrder
                                 : machineIsRunning() ? MachineStatus::Running
                                                      : MachineStatus::Idle;
    HeartbeatResponse hb = api->sendHeartbeat(currentStatus);

    if (!hb.ok) {
        Serial.println("[Heartbeat] Failed — will retry next cycle");


        // Heartbeat hasn't succeeded in too long (server unreachable)
        if (lastHeartbeat - lastSuccessfulHeartbeat > HEARTBEAT_TIMEOUT_MS) {
            Serial.println("[Heartbeat] Timeout — rebooting.");
            ESP.restart();
        }

        return;
    }

    lastSuccessfulHeartbeat =  millis();

    switch (hb.command) {
        case MachineCommand::None:                                         break;
        case MachineCommand::TryAgain:    startMachineCycle();            break;
        case MachineCommand::AddTime:     addCycleTime(hb.commandPayload.toInt()); break;
        case MachineCommand::Cancel:      cancelCycle();                  break;
        case MachineCommand::UpdateConfig: applyConfig(hb.commandPayload); break;
        case MachineCommand::UpdateFirmware:
            Serial.println("[OTA] Update command received.");
            #if defined(HAS_DISPLAY)
                // TODO: showUpdating();
            #endif
            api->checkAndApplyFirmwareUpdate(
                hb.commandPayload.isEmpty() ? String(FIRMWARE_PROFILE) : hb.commandPayload,
                String(FIRMWARE_VERSION));
            break;
        case MachineCommand::Reboot:
#if defined(HAS_DISPLAY)
            showRebooting();
#endif
            delay(1000);
            ESP.restart();
            break;
        case MachineCommand::OutOfOrder:
            Serial.println("[Machine] Out of order.");
            isOutOfOrder = true;
            #if defined(HAS_DISPLAY)
                showOutOfOrder();
            #endif
            break;
        case MachineCommand::InOrder:
            Serial.println("[Machine] Back in order.");
            isOutOfOrder = false;
            #if defined(HAS_DISPLAY)
                showReady();
            #endif
            break;
    }
}


// ─────────────────────────────────────────────────────────────────────────────
// Kiosk state machine
// ─────────────────────────────────────────────────────────────────────────────

enum class KioskState : uint8_t {
    IDLE,
    BALANCE_SHOWN,       // balance on screen, Admin button visible
    ADMIN_MENU,          // Add / Deduct / Back
    ADMIN_AMOUNT,        // numpad — enter dollar amount
    ADMIN_AWAITING_CARD, // waiting for admin tap to authorise
    ADMIN_RESULT,        // brief success/fail screen
};

static KioskState       kioskState       = KioskState::IDLE;
static unsigned long    stateEnteredMs   = 0;
static String           pendingCardId    = "";    // customer card receiving funds
static float            pendingAmount    = 0.0f;  // amount entered on numpad
static String           amountStr       = "";    // raw digit string while typing
static bool             pendingIsDeduct  = false; // true = deduct, false = add

static void enterState(KioskState s) {
    kioskState     = s;
    stateEnteredMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
// Touch helpers
// XPT2046 with setRotation(3): raw X maps to screen X (0-480),
// raw Y maps to screen Y (0-320).  Calibration is approximate — adjust the
// min/max constants if your panel reads differently.
// ─────────────────────────────────────────────────────────────────────────────

#if defined(HAS_TOUCH)
static const int TS_MINX = 200,  TS_MAXX = 3800;
static const int TS_MINY = 200,  TS_MAXY = 3800;
static const int SCR_W   = 480,  SCR_H   = 320;

struct TPoint { int x; int y; bool valid; };

static TPoint getTouchPoint() {
    if (!touch->tirqTouched() || !touch->touched())
        return {0, 0, false};
    TS_Point p = touch->getPoint();
    int sx = map(p.x, TS_MINX, TS_MAXX, 0, SCR_W);
    int sy = map(p.y, TS_MINY, TS_MAXY, 0, SCR_H);
    sx = constrain(sx, 0, SCR_W - 1);
    sy = constrain(sy, 0, SCR_H - 1);
    
    Serial.printf("\n[Touch] Touchpoint: %d, %d\n", sx, sy);

    return {sx, sy, true};
}

static bool hitTest(int tx, int ty, int bx, int by, int bw, int bh) {
    return tx >= bx && tx < bx + bw && ty >= by && ty < by + bh;
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Screen drawing helpers
// ─────────────────────────────────────────────────────────────────────────────

#if defined(HAS_DISPLAY)

// Draws a filled rounded button with centred label
static void drawButton(int x, int y, int w, int h,
                       uint16_t bg, uint16_t fg,
                       const char* label, uint8_t textSize = 2) {
                    
    Serial.printf("[Display] Draw Button: %d, %d, %d, %d\n", x, y, w, h);
    
    gfx->fillRoundRect(x, y, w, h, 6, bg);
    gfx->drawRoundRect(x, y, w, h, 6, fg);
    gfx->setTextSize(textSize);
    gfx->setTextColor(fg, bg);
    int16_t x1, y1; uint16_t tw, th;
    gfx->getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor(x + (w - tw) / 2 - x1, y + (h - th) / 2 - y1);
    gfx->print(label);
}

// ── Admin button geometry (top-right corner) ──────────────────────────────────
#define ADMIN_BTN_X  370
#define ADMIN_BTN_Y    6
#define ADMIN_BTN_W   104
#define ADMIN_BTN_H    36

static void drawAdminButton() {
    drawButton(ADMIN_BTN_X, ADMIN_BTN_Y, ADMIN_BTN_W, ADMIN_BTN_H,
               0x2104 /*dark grey*/, WHITE, "Admin", 2);
}

#endif // HAS_DISPLAY

// ─────────────────────────────────────────────────────────────────────────────
// Idle screen
// ─────────────────────────────────────────────────────────────────────────────

static void showKioskIdle() {
    pendingCardId = "";
    pendingAmount = 0.0f;
    amountStr     = "";
    enterState(KioskState::IDLE);
#if defined(HAS_DISPLAY)
    gfx->fillScreen(BLACK);
    gfx->setTextColor(GREEN, BLACK);
    gfx->setTextSize(4);
    gfx->setCursor(80, 130);
    gfx->print("Check your");
    gfx->setCursor(60, 185);
    gfx->print("Balance here");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Balance screen  (with Admin button)
// ─────────────────────────────────────────────────────────────────────────────

static void showBalance(float balance, const String& cardId) {
    pendingCardId = cardId;
    enterState(KioskState::BALANCE_SHOWN);
#if defined(HAS_DISPLAY)
    gfx->fillScreen(BLACK);

    // "Balance" label
    gfx->setTextSize(4);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(140, 100);
    gfx->print("Balance");

    // Dollar amount — large, centred
    char buf[16];
    snprintf(buf, sizeof(buf), "$%.2f", balance);
    gfx->setTextSize(8);
    gfx->setTextColor(GREEN, BLACK);
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor((SCR_W - w) / 2 - x1, 175);
    gfx->print(buf);

    drawAdminButton();
#endif
}

// ── Admin menu ───────────────────────────────────────────────────────────────
#define MENU_BTN_W  130
#define MENU_BTN_H   70
#define MENU_BTN_Y  110
#define MENU_ADD_X   30
#define MENU_DED_X  175
#define MENU_BCK_X  320

#if defined(HAS_DISPLAY)
static void drawAdminMenu() {
    gfx->fillScreen(BLACK);
    gfx->setTextSize(3);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(140, 40);
    gfx->print("Admin");

    drawButton(MENU_ADD_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 0x07E0 /*green*/, BLACK, "Add",    3);
    drawButton(MENU_DED_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 0xF800 /*red*/,   BLACK, "Refund", 3);
    drawButton(MENU_BCK_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H, 0x2104 /*grey*/,  WHITE, "Back",   3);
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Admin — amount entry (numpad)
// ─────────────────────────────────────────────────────────────────────────────
//
// Layout (480×320):
//   Title bar                          y=0–40
//   Amount display                     y=50–100
//   Numpad  3×4 grid  centred         y=110–295
//   Cancel button      bottom-left    y=284–314

// Numpad button size & origin
#define NP_BTN_W  80
#define NP_BTN_H  48
#define NP_COLS    4   // 1-9 + ".", 0, ⌫, Enter
#define NP_ROWS    3
#define NP_ORIG_X  40
#define NP_ORIG_Y 111
#define NP_GAP      6

// Cancel button — sits below the Enter/OK button
#define CANCEL_BTN_X  (ENTER_BTN_X)
#define CANCEL_BTN_Y  (ENTER_BTN_Y + ENTER_BTN_H + NP_GAP)
#define CANCEL_BTN_W  NP_BTN_W
#define CANCEL_BTN_H  40

// Enter button (rightmost column, spans rows 1-3)
#define ENTER_BTN_X  (NP_ORIG_X + 3*(NP_BTN_W + NP_GAP))
#define ENTER_BTN_Y  NP_ORIG_Y
#define ENTER_BTN_W  NP_BTN_W
#define ENTER_BTN_H  (3*NP_BTN_H + 2*NP_GAP)

// Key layout: row-major, 3 rows × 3 digit cols  (Enter is separate)
static const char NP_KEYS[3][3] = {
    {'1','2','3'},
    {'4','5','6'},
    {'7','8','9'},
};
// Bottom row: '.' '0' 'B' (backspace)
static const char NP_BOT[3] = {'.','0','B'};

#if defined(HAS_DISPLAY)
static void drawNumpad() {
    gfx->fillScreen(BLACK);

    // Title
    gfx->setTextSize(3);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(10, 8);
    gfx->print(pendingIsDeduct ? "Refund from card" : "Add funds to card");

    // Amount display box
    gfx->drawRoundRect(NP_ORIG_X, 54, 3*(NP_BTN_W+NP_GAP)-NP_GAP + NP_BTN_W, 52, 4, WHITE);
    gfx->setTextSize(4);
    gfx->setTextColor(YELLOW, BLACK);
    String display = "$" + (amountStr.isEmpty() ? String("0.00") : amountStr);
    gfx->setCursor(NP_ORIG_X + 10, 64);
    gfx->fillRect(NP_ORIG_X+1, 55, 3*(NP_BTN_W+NP_GAP)-NP_GAP + NP_BTN_W - 2, 50, BLACK);
    gfx->print(display);

    // Digit rows
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            int bx = NP_ORIG_X + c*(NP_BTN_W+NP_GAP);
            int by = NP_ORIG_Y + r*(NP_BTN_H+NP_GAP);
            char label[2] = { NP_KEYS[r][c], '\0' };
            drawButton(bx, by, NP_BTN_W, NP_BTN_H, 0x2104, WHITE, label, 3);
        }
    }
    // Bottom row: . 0 ⌫
    for (int c = 0; c < 3; c++) {
        int bx = NP_ORIG_X + c*(NP_BTN_W+NP_GAP);
        int by = NP_ORIG_Y + 3*(NP_BTN_H+NP_GAP);
        char label[3];
        if (NP_BOT[c] == 'B') { label[0]='<'; label[1]='<'; label[2]='\0'; }
        else                  { label[0]=NP_BOT[c]; label[1]='\0'; }
        drawButton(bx, by, NP_BTN_W, NP_BTN_H, 0x2104, WHITE, label, 3);
    }
    // Enter button (tall, right column)
    drawButton(ENTER_BTN_X, ENTER_BTN_Y, ENTER_BTN_W, ENTER_BTN_H, GREEN, BLACK, "OK", 3);

    // Cancel
    drawButton(CANCEL_BTN_X, CANCEL_BTN_Y, CANCEL_BTN_W, CANCEL_BTN_H, RED, WHITE, "Cancel", 2);
}

// Redraw just the amount field (called after each keypress)
static void refreshAmountDisplay() {
    gfx->fillRect(NP_ORIG_X+1, 55, 3*(NP_BTN_W+NP_GAP)+NP_BTN_W-2, 50, BLACK);
    gfx->setTextSize(4);
    gfx->setTextColor(YELLOW, BLACK);
    String display = "$" + (amountStr.isEmpty() ? String("0.00") : amountStr);
    gfx->setCursor(NP_ORIG_X + 10, 64);
    gfx->print(display);
}
#endif // HAS_DISPLAY

// Process a numpad key press — returns true if Enter was pressed
static bool handleNumpadKey(char key) {
    if (key == 'B') {
        // Backspace
        if (!amountStr.isEmpty()) amountStr.remove(amountStr.length()-1);
    } else if (key == '.') {
        // Only one decimal point; ignore if already present
        if (amountStr.indexOf('.') < 0 && amountStr.length() < 7)
            amountStr += '.';
    } else if (key == 'E') {
        // Enter — validate
        float val = amountStr.toFloat();
        if (val > 0.0f) {
            pendingAmount = val;
            return true;
        }
    } else {
        // Digit — limit to 7 chars (e.g. "999.99")
        if (amountStr.length() < 7) amountStr += key;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Admin — awaiting admin card tap
// ─────────────────────────────────────────────────────────────────────────────

#if defined(HAS_DISPLAY)
static void drawAwaitingAdminCard() {
    gfx->fillScreen(BLACK);

    gfx->setTextSize(3);
    gfx->setTextColor(YELLOW, BLACK);
    gfx->setCursor(50, 60);
    gfx->print(pendingIsDeduct ? "Refunding $" : "Adding $");
    gfx->print(pendingAmount, 2);

    gfx->setTextSize(4);
    gfx->setTextColor(WHITE, BLACK);
    gfx->setCursor(50, 130);
    gfx->print("Tap admin card");
    gfx->setCursor(80, 185);
    gfx->print("to confirm");

    drawButton(CANCEL_BTN_X, CANCEL_BTN_Y, CANCEL_BTN_W, CANCEL_BTN_H, RED, WHITE, "Cancel", 2);
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Admin — result screen
// ─────────────────────────────────────────────────────────────────────────────

#define RESULT_DISPLAY_MS  4000UL

#if defined(HAS_DISPLAY)
static void drawAdminResult(bool success, float newBalance, const String& errMsg) {
    gfx->fillScreen(BLACK);
    if (success) {
        gfx->setTextSize(5);
        gfx->setTextColor(GREEN, BLACK);
        gfx->setCursor(130, 90);
        gfx->print("Done!");

        char buf[20];
        snprintf(buf, sizeof(buf), "New bal: $%.2f", newBalance);
        gfx->setTextSize(3);
        gfx->setTextColor(WHITE, BLACK);
        gfx->setCursor(60, 175);
        gfx->print(buf);
    } else {
        gfx->setTextSize(4);
        gfx->setTextColor(RED, BLACK);
        gfx->setCursor(80, 110);
        gfx->print(errMsg);
    }
    enterState(KioskState::ADMIN_RESULT);
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[Boot] LaundryKiosk starting.");

    // Drive RFID_SS high immediately so the MFRC522 is deselected
    // before the SPI bus is initialised. A floating CS corrupts touch reads.
    // pinMode(RFID_SS, OUTPUT);
    // digitalWrite(RFID_SS, HIGH);

#if defined(HAS_DISPLAY)
    setupDisplay();
#endif

#if defined(HAS_TOUCH)
    setupTouch();
#endif

    // ── RFID ──────────────────────────────────────────────────────────────────
    // Note: touchSPI (HSPI) is already begun in setupTouch() — setupRFID()
    // piggybacks on the same SPIClass instance. No second SPI.begin() here.
    setupRFID();


    // ── Provisioning check ────────────────────────────────────────────────────
    if (!loadProvisionData(provData)) {
        Serial.println("[Boot] Not provisioned — entering setup mode.");
#if defined(HAS_DISPLAY)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        char apName[32];
        snprintf(apName, sizeof(apName), "OrbitSetup-%02X%02X%02X", mac[3], mac[4], mac[5]);
        showProvisioning(apName);
#endif
        runProvisioningMode();  // blocks until done, then reboots
    }

    // ── WiFi ──────────────────────────────────────────────────────────────────
    connectWifi();

    // ── API client ────────────────────────────────────────────────────────────
    api = new ApiClient(provData.apiHost, provData.apiPort);

    // ── Auth ──────────────────────────────────────────────────────────────────
    showLoggingIn();
    bool credLoaded = api->loadCredentials();
    bool authed     = false;

    if (credLoaded && !api->needsRelogin()) {
        if (api->needsRefresh()) api->refreshToken();
        authed = true;
        showLoggedIn();
    } else {
        authed = api->login(provData.apiUser, provData.apiPass);
        if (authed) showLoggedIn();
    }

    if (!authed) {
        Serial.println("[Boot] Login failed — rebooting in 5s.");
        delay(5000);
        ESP.restart();
    }


    // ── Initial heartbeat ─────────────────────────────────────────────────────
    doHeartbeat();

    // ── Idle screen ───────────────────────────────────────────────────────────
    showKioskIdle();

    Serial.println("[Boot] Ready.");
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();

    // ── Heartbeat ────────────────────────────────────────────────────────────
    doHeartbeat();

    // ── WiFi watchdog ─────────────────────────────────────────────────────────
    if (WiFi.status() != WL_CONNECTED) {
        if (now - lastWifiCheck > 30000UL) {
            lastWifiCheck = now;
            Serial.println("[WiFi] Still disconnected...");
            if (now - lastWifiConnected > 120000UL) {
                Serial.println("[WiFi] Rebooting.");
                ESP.restart();
            }
        }
    } else {
        lastWifiConnected = now;
    }

    // ── Token refresh ─────────────────────────────────────────────────────────
    if      (api->needsRelogin())  api->login(provData.apiUser, provData.apiPass);
    else if (api->needsRefresh() && !api->refreshToken())  api->login(provData.apiUser, provData.apiPass);

    // ── BALANCE_SHOWN / ADMIN_RESULT timeout → back to IDLE ─────────────────
    if (kioskState == KioskState::BALANCE_SHOWN &&
        now - stateEnteredMs > BALANCE_DISPLAY_MS)
    {
        showKioskIdle();
        return;
    }
    if (kioskState == KioskState::ADMIN_RESULT &&
        now - stateEnteredMs > RESULT_DISPLAY_MS)
    {
        showKioskIdle();
        return;
    }

    // ── Touch handling ────────────────────────────────────────────────────────
#if defined(HAS_TOUCH)
    static bool lastTouched = false;
    TPoint tp = getTouchPoint();

    // Edge-detect: only fire once on press-down, ignore held touch
    if (tp.valid && !lastTouched) {
        lastTouched    = true;
        lastScreenTime = now;

        switch (kioskState) {

        case KioskState::BALANCE_SHOWN:
            // Admin button — top right
            if (hitTest(tp.x, tp.y, ADMIN_BTN_X, ADMIN_BTN_Y, ADMIN_BTN_W, ADMIN_BTN_H)) {
                Serial.println("[Admin] Admin button pressed.");
                enterState(KioskState::ADMIN_MENU);
#if defined(HAS_DISPLAY)
                drawAdminMenu();
#endif
            }
            break;

        case KioskState::ADMIN_MENU:
            if (hitTest(tp.x, tp.y, MENU_ADD_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H)) {
                Serial.println("[Admin] Add selected.");
                pendingIsDeduct = false;
                amountStr = "";
                enterState(KioskState::ADMIN_AMOUNT);
#if defined(HAS_DISPLAY)
                drawNumpad();
#endif
            } else if (hitTest(tp.x, tp.y, MENU_DED_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H)) {
                Serial.println("[Admin] Refund selected.");
                pendingIsDeduct = true;
                amountStr = "";
                enterState(KioskState::ADMIN_AMOUNT);
#if defined(HAS_DISPLAY)
                drawNumpad();
#endif
            } else if (hitTest(tp.x, tp.y, MENU_BCK_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H)) {
                Serial.println("[Admin] Back pressed.");
                // Re-show balance screen — re-fetch so it's fresh
                float bal;
                if (api->getBalance(pendingCardId, bal))
                    showBalance(bal, pendingCardId);
                else
                    showKioskIdle();
            }
            break;

        case KioskState::ADMIN_AMOUNT: {
            // Cancel
            if (hitTest(tp.x, tp.y, CANCEL_BTN_X, CANCEL_BTN_Y, CANCEL_BTN_W, CANCEL_BTN_H)) {
                Serial.println("[Admin] Cancelled.");
                showKioskIdle();
                break;
            }
            // Enter button
            if (hitTest(tp.x, tp.y, ENTER_BTN_X, ENTER_BTN_Y, ENTER_BTN_W, ENTER_BTN_H)) {
                if (handleNumpadKey('E')) {
                    enterState(KioskState::ADMIN_AWAITING_CARD);
#if defined(HAS_DISPLAY)
                    drawAwaitingAdminCard();
#endif
                }
                break;
            }
            // Digit rows
            char pressedKey = 0;
            for (int r = 0; r < 3 && !pressedKey; r++) {
                for (int c = 0; c < 3 && !pressedKey; c++) {
                    int bx = NP_ORIG_X + c*(NP_BTN_W+NP_GAP);
                    int by = NP_ORIG_Y + r*(NP_BTN_H+NP_GAP);
                    if (hitTest(tp.x, tp.y, bx, by, NP_BTN_W, NP_BTN_H))
                        pressedKey = NP_KEYS[r][c];
                }
            }
            // Bottom row
            if (!pressedKey) {
                int by = NP_ORIG_Y + 3*(NP_BTN_H+NP_GAP);
                for (int c = 0; c < 3 && !pressedKey; c++) {
                    int bx = NP_ORIG_X + c*(NP_BTN_W+NP_GAP);
                    if (hitTest(tp.x, tp.y, bx, by, NP_BTN_W, NP_BTN_H))
                        pressedKey = NP_BOT[c];
                }
            }
            if (pressedKey) {
                handleNumpadKey(pressedKey);
#if defined(HAS_DISPLAY)
                refreshAmountDisplay();
#endif
            }
            break;
        }

        case KioskState::ADMIN_AWAITING_CARD:
            // Cancel only — card tap is handled below in the RFID block
            if (hitTest(tp.x, tp.y, CANCEL_BTN_X, CANCEL_BTN_Y, CANCEL_BTN_W, CANCEL_BTN_H)) {
                Serial.println("[Admin] Awaiting-card cancelled.");
                showKioskIdle();
            }
            break;

        default:
            break;
        }
    }
    if (!tp.valid) lastTouched = false;
#endif // HAS_TOUCH

    // ── RFID scan ──────────────────────────────────────────────────────────────
    if (now - lastTapTime > TAP_COOLDOWN_MS && rfidPoll())
    {
        lastTapTime    = now;
        lastScreenTime = now;
        String cardID  = printUid(mfrc522->uid);
        mfrc522->PICC_HaltA();
        mfrc522->PCD_StopCrypto1();
        Serial.println("[RFID] Tap: " + cardID);

        if (kioskState == KioskState::ADMIN_AWAITING_CARD) {
            // ── Admin card verification + fund transfer ──────────────────
            Serial.println("[Admin] Checking card: " + cardID);
            if (!api->isAdmin(cardID)) {
                Serial.println("[Admin] Not an admin card.");
#if defined(HAS_DISPLAY)
                drawAdminResult(false, 0.0f, "Not admin card");
#endif
            } else {
                float newBalance = 0.0f;
                bool ok;
                if (pendingIsDeduct) {
                    String result, displayName;
                    String accountId;
                    ok = api->refund(pendingCardId, accountId, pendingAmount,
                                     String("Cash"), result, newBalance, displayName);
                    Serial.printf("[Admin] refund %s  msg=%s  newBal=%.2f\n",
                                  ok ? "OK" : "FAIL", result.c_str(), newBalance);
                } else {
                    ok = api->addFunds(pendingCardId, pendingAmount, newBalance);
                    Serial.printf("[Admin] addFunds %s  newBal=%.2f\n",
                                  ok ? "OK" : "FAIL", newBalance);
                }
#if defined(HAS_DISPLAY)
                if (ok) drawAdminResult(true,  newBalance, "");
                else    drawAdminResult(false, 0.0f, "Transfer failed");
#endif
            }
        } else {
            // ── Normal tap: show balance ───────────────────────────────
            float bal;
            if (!api->getBalance(cardID, bal)) {
                Serial.println("[Tap] Balance lookup failed.");
#if defined(HAS_DISPLAY)
                gfx->fillScreen(BLACK);
                gfx->setTextSize(3);
                gfx->setTextColor(RED, BLACK);
                gfx->setCursor(80, 160);
                gfx->print("Card not found");
#endif
                enterState(KioskState::BALANCE_SHOWN); // still times out to idle
            } else {
                Serial.printf("[Tap] Balance: $%.2f\n", bal);
                showBalance(bal, cardID);
            }
        }
    }
}
