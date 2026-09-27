#include <main.h>

// ── Provisioned credentials ───────────────────────────────────────────────────
ProvisionData provData;

// ── RFID objects ──────────────────────────────────────────────────────────────
// Shares the HSPI bus with touch via the touchSPI SPIClass instance in Touch.cpp
// Touch.cpp must be compiled (HAS_TOUCH) OR touchSPI must be defined here
// if HAS_TOUCH is disabled. See setupRFID() below.

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

// ── Accumulation state ────────────────────────────────────────────────────────
String        accumulatingCardId    = "";
float         accumulatedAmount     = 0.0f;
int           accumulatedSeconds    = 0;
float         accumulatedBalance    = 0.0f;
unsigned long lastAccumRead         = 0;
bool          isAccumulating        = false;

const unsigned long ACCUM_COMMIT_MS = TAP_COOLDOWN_MS + ACCUM_COMMIT_OFFSET_MS;


// ─────────────────────────────────────────────────────────────────────────────
// WiFi
// ─────────────────────────────────────────────────────────────────────────────

void connectWifi() {
#if defined(HAS_DISPLAY)
    showConnecting();
#endif
    Serial.println("[WiFi] Connecting...");

    // Disable Wi-Fi Power Save completely
    esp_wifi_set_ps(WIFI_PS_NONE);

    int waitTimeout = 0;
    WiFi.begin(provData.wifiSsid.c_str(), provData.wifiPass.c_str());
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        waitTimeout += 100;
        
        if (waitTimeout > 30000) {
            Serial.println("[WiFi] Failed to connect.");
            if (!provData.verified) {
                Serial.println("[WiFi] Never verified — clearing provisioning.");
                clearProvisionData();
            } else {
                Serial.println("[WiFi] Previously verified — rebooting to retry.");
            }
            #if defined(HAS_DISPLAY)
                showRebooting();
            #endif
            delay(1000);
            ESP.restart();
        }
    }

#if defined(HAS_DISPLAY)
    showConnected();
#endif
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] IP: "); Serial.println(WiFi.localIP());

    // DNS diagnostics
    IPAddress serverIP;
    Serial.print("[DNS] Resolving: "); Serial.println(testInitDnsHost);
    if (WiFi.hostByName(testInitDnsHost, serverIP))
        Serial.println("[DNS] OK: " + serverIP.toString());
    else
        Serial.println("[DNS] Failed: " + String(testInitDnsHost));

    Serial.print("[DNS] Resolving: "); Serial.println(provData.apiHost.c_str());
    if (WiFi.hostByName(provData.apiHost.c_str(), serverIP))
        Serial.println("[DNS] OK: " + serverIP.toString());
    else
        Serial.println("[DNS] Failed: " + String(API_HOST));
}


// ─────────────────────────────────────────────────────────────────────────────
// RFID
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

void resetRFIDModule() {
    Serial.println("[RFID] Health check...");
    if (!rfidHealthy() && !mfrc522->PCD_Init())
        Serial.println("[RFID] Re-init failed.");
}

bool rfidHealthy() {
    byte version = mfrc522->PCD_GetVersion();
    Serial.print("[RFID] Version: "); Serial.println(version);
    return (version != 0x00 && version != 0xFF);
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


// ─────────────────────────────────────────────────────────────────────────────
// Machine control
// ─────────────────────────────────────────────────────────────────────────────

bool machineBusyPin(){
#if defined(HAS_MACHINE_BUSY)
    return MACHINE_BUSY_HIGH ? digitalRead(MACHINE_BUSY) 
                                 : !digitalRead(MACHINE_BUSY);
#else
    return false;
#endif
}

bool machineIsRunning() {
#if defined(HAS_MACHINE_BUSY)
    if (useMachineBusy)
        return machineBusyPin();
#endif
    return (countdownSeconds > 0);
}

void startMachineCycle() {
    if (coinMode) {
        for (int i = 0; i < coinCount; i++) {
            delay(coinPulseDelay);
            digitalWrite(RELAY1, RELAY_ACTIVE);
            delay(coinPulseDuration);
            digitalWrite(RELAY1, !RELAY_ACTIVE);
        }
    } else {
        digitalWrite(RELAY1, RELAY_ACTIVE);
        delay(200);
        digitalWrite(RELAY1, !RELAY_ACTIVE);
    }
}

void addCycleTime(int seconds) {
    bool wasRunning = (countdownSeconds > 0);

    if (!wasRunning) {
        // Fresh start — set the clock reference now before anything else
        countdownTotalSeconds = seconds;
        countdownStartMs      = millis();
        countdownSeconds      = seconds;
    } else {
        // Already running — extend the total, preserve elapsed time
        countdownTotalSeconds += seconds;
        int elapsed = (int)((millis() - countdownStartMs) / 1000);
        countdownSeconds = max(0, countdownTotalSeconds - elapsed);
    }

    if (countdownTotalSeconds > maxSeconds) maxSeconds = countdownTotalSeconds;
}

void cancelCycle() {
    countdownSeconds = 1; // let the countdown tick handler do the cleanup
}


// ─────────────────────────────────────────────────────────────────────────────
// Accumulation
// ─────────────────────────────────────────────────────────────────────────────

void commitAccumulation() {
    if (!isAccumulating || accumulatedAmount <= 0) {
        isAccumulating     = false;
        accumulatingCardId = "";
        return;
    }

    Serial.println("[Accum] Committing $" + String(accumulatedAmount, 2)
                   + " for " + String(accumulatedSeconds) + "s");

    String msg, display;
    float  newBal;

    if (api->deduct(accumulatingCardId, "00000000-0000-0000-0000-000000000000",
                    accumulatedAmount, "CASH", msg, newBal, display)) {

        // Time was already added optimistically on each tap — just start
        // the machine if it's not already running
        if (!machineIsRunning()) startMachineCycle();

        optimisticSeconds = 0;
        tapSuccess(newBal);
        Serial.println("[Accum] OK. New balance: " + String(newBal));
    } else {
        // Roll back the optimistically added time
        if (optimisticSeconds > 0) {
            Serial.printf("[Accum] Rolling back %ds of optimistic time.\n", optimisticSeconds);
            countdownTotalSeconds = max(0, countdownTotalSeconds - optimisticSeconds);
            int elapsed = (int)((millis() - countdownStartMs) / 1000);
            countdownSeconds = max(0, countdownTotalSeconds - elapsed);
            optimisticSeconds = 0;
        }
        tapDeductFailed();
        Serial.println("[Accum] Failed: " + msg);
    }

    isAccumulating     = false;
    accumulatingCardId = "";
    accumulatedAmount  = 0.0f;
    accumulatedSeconds = 0;
    accumulatedBalance = 0.0f;
    lastAccumRead      = 0;
    optimisticSeconds  = 0;

#if defined(HAS_DISPLAY)
    DrawHeader();  // restore "Tap laundry card" once accumulation is done
#endif
}


// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

void applyConfig(const String& json) {
    if (json.isEmpty()) return;

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("[Config] Parse error: ");
        Serial.println(err.c_str());
        return;
    }

    if (doc["timerMode"].is<bool>())               timerMode           = doc["timerMode"];
    if (doc["cycleLengthSeconds"].is<unsigned long>()) cycleLengthSeconds  = doc["cycleLengthSeconds"].as<unsigned long>();
    if (doc["amount"].is<double>())                   Amount              = doc["amount"].as<double>();
    if (doc["coinMode"].is<bool>())                   coinMode            = doc["coinMode"];
    if (doc["coinCount"].is<int>())                   coinCount           = doc["coinCount"];
    if (doc["coinPulseDuration"].is<int>())           coinPulseDuration   = doc["coinPulseDuration"];
    if (doc["coinPulseDelay"].is<int>())              coinPulseDelay      = doc["coinPulseDelay"];
    if (doc["useMachineBusy"].is<bool>())             useMachineBusy      = doc["useMachineBusy"];
    if (doc["busyCooldownSeconds"].is<int>())         busyCooldownSeconds = doc["busyCooldownSeconds"];
    if (doc["screenTimeout"].is<int>())               screenTimeout       = doc["screenTimeout"];

    if (doc["timeRemaining"].is<int>()) {
    int remaining = doc["timeRemaining"];
    if (remaining > 0) {
        countdownTotalSeconds = remaining;
        countdownStartMs      = millis();
        countdownSeconds      = remaining;
        if (countdownSeconds > maxSeconds) maxSeconds = countdownSeconds;
    }
    Serial.printf("  Resume Cycle=%d\n", remaining);
}

    Serial.println("[Config] Applied:");
    Serial.printf("  timerMode=%d  amount=%.2f  cycleLengthSeconds=%lu\n",
                  timerMode, Amount, cycleLengthSeconds);
    Serial.printf("  coinMode=%d  coinCount=%d  useMachineBusy=%d\n",
                  coinMode, coinCount, useMachineBusy);
    Serial.printf("  screenTimeout=%d  busyCooldown=%d\n",
                  screenTimeout, busyCooldownSeconds);
}

void fetchConfig() {
    Serial.println("[Config] Fetching...");
    String response;
    if (!api->get("/machineconfig/current", response, true)) {
        Serial.println("[Config] Fetch failed: " + response);
        return;
    }
    applyConfig(response);
}


// ─────────────────────────────────────────────────────────────────────────────
// Heartbeat
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
// Setup
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // ── Provisioning check ────────────────────────────────────────────────────
    #if defined(CLEAR_PROVISION)
        clearProvisionData();
    #endif

    if (!loadProvisionData(provData)) {
        Serial.println("[Provision] Not provisioned — entering setup mode.");
        #if defined(HAS_DISPLAY)
            setupDisplay();
            // Build the AP name the same way Provision.h does
            uint8_t mac[6];
            esp_read_mac(mac, ESP_MAC_WIFI_STA);
            char apName[32];
            snprintf(apName, sizeof(apName), "OrbitSetup-%02X%02X%02X",
                     mac[3], mac[4], mac[5]);
            showProvisioning(String(apName));
        #endif
        runProvisioningMode();  // blocks until done, then reboots
        return;
    }

    Serial.println("[Provision] Loaded: " + provData.machineName);

    esp_reset_reason_t reason = esp_reset_reason();
    Serial.print("[BOOT] Reset reason: ");
    Serial.println(reason);  
    // 1=POWERON, 3=SW, 4=PANIC, 5=INT_WDT, 6=TASK_WDT, 7=WDT, 8=DEEPSLEEP, 9=BROWNOUT

Serial.print("[BOOT] Free heap: ");
Serial.println(esp_get_free_heap_size());
Serial.print("[BOOT] Min free heap: ");
Serial.println(esp_get_minimum_free_heap_size());

    pinMode(RELAY1, OUTPUT);

    #if defined(RELAY_ON_START)
    digitalWrite(RELAY1, RELAY_ACTIVE);
    #else
    digitalWrite(RELAY1, !RELAY_ACTIVE);
    #endif

#if defined(HAS_MACHINE_BUSY)
    pinMode(MACHINE_BUSY, INPUT);
#endif

#if defined(HAS_DISPLAY)
    setupDisplay();
#endif

#if defined(HAS_TOUCH)
    setupTouch();
#endif

    setupFeedback();
    setupRFID();
    connectWifi();

    api = new ApiClient(provData.apiHost, provData.apiPort);

#if defined(HAS_DISPLAY)
    showLoggingIn();
#endif
    Serial.println("[Auth] Logging in...");

    if (ensureAuthenticated()) {
        Serial.println("[Auth] Logged in!");
#if defined(HAS_DISPLAY)
        showLoggedIn();
#endif

        lastConfigFetch = millis();
        fetchConfig();

        // Check for firmware update on every boot
        Serial.println("[OTA] Checking for firmware update...");
        api->checkAndApplyFirmwareUpdate(FIRMWARE_PROFILE, FIRMWARE_VERSION);
        // If update applies, device reboots here automatically

#if defined(HAS_DISPLAY)
        delay(2000);
        showReady();
#endif
    } else {
        Serial.println("[Auth] Login failed!");
        clearProvisionData();
        #if defined(HAS_DISPLAY)
            showRebooting();
        #endif
            delay(1000);
            ESP.restart();
    }

    lastTapTime    = millis();
    lastTick       = millis();
    lastScreenTime = millis();
    lastSuccessfulHeartbeat = millis();
    lastWifiCheck = millis();
    lastWifiConnected = millis();
}


// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────

void loop() {

    // ── Touch ─────────────────────────────────────────────────────────────────
#if defined(HAS_TOUCH)
    if (isTouched()) {
        lastScreenTime = millis();
        // Future: handle touch input here
    }
#endif

    // ── Heartbeat ─────────────────────────────────────────────────────────────
    doHeartbeat();

    // ── Backlight ─────────────────────────────────────────────────────────────
#if defined(HAS_DISPLAY)
    bool recentActivity = (countdownSeconds > 0)
                       || (millis() - lastScreenTime < (unsigned long)screenTimeout);
    setBacklight(recentActivity);
#endif

    // ── Token refresh ─────────────────────────────────────────────────────────
    if      (api->needsRelogin())  api->login(provData.apiUser, provData.apiPass);
    else if (api->needsRefresh())  api->refreshToken();

    // ── Message clear ─────────────────────────────────────────────────────────
#if defined(HAS_DISPLAY)
    if (messageTime && millis() - messageTime > 1000) {
        messageTime = 0;
        if (countdownSeconds == 0 && !isAccumulating) {
            gfx->fillRect(0, 20, 480, 20, BLACK);
            runOnce = true;
        }
    }

    // ── Balance bar timeout ───────────────────────────────────────────────────
    if (balanceDisplayTime && millis() - balanceDisplayTime > BALANCE_DISPLAY_MS)
        clearBalanceBar();
#endif

    // ── Periodic RFID health check ────────────────────────────────────────────
    unsigned long now = millis();
    if (now - lastWakeupTime > WAKEUP_COOLDOWN_MS) {
        resetRFIDModule();
        lastWakeupTime = now;
    }


    // Safe to call every loop — just checking status, no overhead
if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiCheck > 30000UL) {
        lastWifiCheck = now;
        Serial.println("[WiFi] Still disconnected...");
        // Only reboot if disconnected for a long time AND not in countdown
        if (now - lastWifiConnected > 120000UL  && !isAccumulating && countdownSeconds == 0) {
            Serial.println("[WiFi] Rebooting.");
            ESP.restart();
        }
    }
} else {
    lastWifiConnected = now;  // update last known good time
}

    // ── Daily config refresh ──────────────────────────────────────────────────
    if (millis() - lastConfigFetch > CONFIG_FETCH_INTERVAL_MS) {
        lastConfigFetch = millis();
        fetchConfig();
    }

    // ── Accumulation commit on card removal ───────────────────────────────────
    if (isAccumulating && (millis() - lastAccumRead > ACCUM_COMMIT_MS)) {
        Serial.println("[Accum] Card gone — committing.");
        commitAccumulation();
    }

    // ── RFID read ─────────────────────────────────────────────────────────────
    if (now - lastTapTime > TAP_COOLDOWN_MS &&
        mfrc522->PICC_IsNewCardPresent() &&
        mfrc522->PICC_ReadCardSerial())
    {
        lastTapTime    = now;
        lastScreenTime = now;
        String cardID  = printUid(mfrc522->uid);
        Serial.print("[RFID] Tap: "); Serial.println(cardID);

#if defined(HAS_LED_RED)
    // Immediate feedback — card detected, not yet read
    digitalWrite(LED_RED_PIN, HIGH);
#endif

        // ── Timer mode ───────────────────────────────────────────────────────
        if (timerMode) {
            if (isAccumulating && cardID == accumulatingCardId) {
                // Same card — try to add another increment
                float nextAmount = accumulatedAmount + (float)Amount;
                if (nextAmount > accumulatedBalance) {
                    tapAccumMax(accumulatedBalance);
                    lastAccumRead = millis();
                } else {
                    accumulatedAmount  = nextAmount;
                    accumulatedSeconds += (int)cycleLengthSeconds;
                    lastAccumRead      = millis();

                    // Optimistically add time to the display so the customer
                    // sees their remaining time increase immediately
                    addCycleTime((int)cycleLengthSeconds);
                    optimisticSeconds += (int)cycleLengthSeconds;

                    if (!machineIsRunning()) startMachineCycle();

#if defined(HAS_DISPLAY)
                    DrawTimer();
                    DrawProgressBar();
#endif

                    tapAccumulating(accumulatedSeconds, accumulatedAmount);
                    Serial.printf("[Accum] +%lus  total: $%.2f  optimistic: +%ds\n",
                                  cycleLengthSeconds, accumulatedAmount, optimisticSeconds);
                }
            } else {
                // New card — commit any previous first
                if (isAccumulating && cardID != accumulatingCardId) {
                    Serial.println("[Accum] Different card — committing previous.");
                    commitAccumulation();
                }

#if defined(HAS_DISPLAY)
                if (!isAccumulating)
                    gfx->fillRect(0, 160, 480, 75, BLACK); // clear "Ready."
#endif

                float bal;
                if (!api->getBalance(cardID, bal)) {
                    tapCardNotFound();
#if defined(HAS_DISPLAY)
                    DrawHeader();
#endif
                    return;
                }

                if (bal < (float)Amount) {
                    tapNSF(bal, (float)Amount);
                    Serial.println("[Accum] NSF on first tap.");
#if defined(HAS_DISPLAY)
                    DrawHeader();
#endif
                    return;
                }

#if defined(HAS_MACHINE_BUSY)
                if (useMachineBusy && machineBusyPin()) {
                    tapMachineBusy();
                    Serial.println("[Accum] Machine busy.");
#if defined(HAS_DISPLAY)
                    DrawHeader();
#endif
                    return;
                }
#endif

                // Start accumulating
                isAccumulating     = true;
                accumulatingCardId = cardID;
                accumulatedBalance = bal;
                accumulatedAmount  = (float)Amount;
                accumulatedSeconds = (int)cycleLengthSeconds;
                lastAccumRead      = millis();

                // Optimistically show the time on first tap too
                addCycleTime((int)cycleLengthSeconds);
                optimisticSeconds = (int)cycleLengthSeconds;
                if (!machineIsRunning()) startMachineCycle();

#if defined(HAS_DISPLAY)
                DrawTimer();
                DrawProgressBar();
#endif

                tapAccumulating(accumulatedSeconds, accumulatedAmount);
#if defined(HAS_DISPLAY)
                ClearHeader();  // hide "Tap laundry card" — balance bar is at top now
#endif
                Serial.printf("[Accum] Started. Bal: $%.2f\n", bal);
            }
        }

        // ── Pay-to-activate mode ─────────────────────────────────────────────
        else {
            // If same card taps again while cooldown is active, re-trigger
            if (accumulatingCardId == cardID && countdownSeconds > 0
#if defined(HAS_MACHINE_BUSY)
                && (!useMachineBusy || !machineBusyPin())
#endif
            ) {
                startMachineCycle();
            } else {
                float bal;
                if (api->getBalance(cardID, bal)) {
                    Serial.printf("[Tap] Balance: %.2f\n", bal);
                }

#if defined(HAS_MACHINE_BUSY)
                if (useMachineBusy && machineBusyPin()) {
                    tapMachineBusy();
                    Serial.println("[Tap] Machine busy.");
#if defined(HAS_DISPLAY)
                    DrawHeader();
#endif
                    return;
                }
#endif

                String msg, display;
                float  newBal;
                if (api->deduct(cardID, "00000000-0000-0000-0000-000000000000",
                                (float)Amount, "CASH", msg, newBal, display)) {
                    tapSuccess(newBal);
                    startMachineCycle();
                    countdownTotalSeconds = busyCooldownSeconds;
                    countdownStartMs      = millis();
                    countdownSeconds   = busyCooldownSeconds;
                    accumulatingCardId = cardID;
                    Serial.println("[Tap] Card OK. Machine started.");
                } else {
                    tapNSF(bal, (float)Amount);
                    Serial.println("[Tap] NSF.");
                }
            }

#if defined(HAS_DISPLAY)
            gfx->fillRect(0, 160, 480, 75, BLACK);
            DrawHeader();
#endif
        }
    }
#if defined(HAS_LED_RED)
    else{
        // Immediate feedback — card detected, not yet read
        digitalWrite(LED_RED_PIN, LOW);
    }
#endif

    // ── Countdown tick ────────────────────────────────────────────────────────
    if (millis() - lastTick >= 1000) {
        lastTick = millis();

        if (countdownSeconds > 0) {
            // Wall clock derived — immune to blocking calls
            int elapsed  = (int)((millis() - countdownStartMs) / 1000);
            countdownSeconds = max(0, countdownTotalSeconds - elapsed);

#if defined(HAS_DISPLAY)
            // Transition from idle → running: clear stale idle text
            if (timesUpMessageDelay <= 0) {
                gfx->fillScreen(BLACK);
                gfx->drawRect(40, 220, 400, 20, WHITE);
                DrawHeader();
                timesUpMessageDelay = 10;
            }
            DrawTimer();
            DrawProgressBar();
#endif
            if (timerMode)
                digitalWrite(RELAY1, RELAY_ACTIVE);

        } else {
            if (timerMode)
                digitalWrite(RELAY1, !RELAY_ACTIVE);
            maxSeconds = 0;

#if defined(HAS_DISPLAY)
            if (timesUpMessageDelay > 0) {
                if (timesUpMessageDelay == 10)
                    showTimesUp();
                timesUpMessageDelay--;
            } else if (timesUpMessageDelay == 0) {
                showReady();
                timesUpMessageDelay = -1;
                lastScreenTime = millis();
            }
            gfx->setTextSize(3);
#endif
        }
    }

    // ── First draw after message clear ────────────────────────────────────────
#if defined(HAS_DISPLAY)
    if (runOnce) {
        runOnce = false;
        gfx->fillScreen(BLACK);
        gfx->drawRect(40, 220, 400, 20, WHITE);
        DrawHeader();
    }
#endif
}