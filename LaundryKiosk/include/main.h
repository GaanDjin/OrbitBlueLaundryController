#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Config.h>
#include <Pins.h>
#include <Globals.h>
#include <Display.h>
#include <Touch.h>
#include <Feedback.h>
#include <ApiClient.h>
#include <HTTPUpdate.h>
#include <Provision.h>

#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>
#include <MFRC522Constants.h>

// ── RFID objects ──────────────────────────────────────────────────────────────
extern MFRC522DriverPinSimple* ss_pin;
extern MFRC522DriverSPI*       driver;
extern MFRC522*                mfrc522;

// ── Accumulation state ────────────────────────────────────────────────────────
extern String        accumulatingCardId;
extern float         accumulatedAmount;
extern int           accumulatedSeconds;
extern float         accumulatedBalance;
extern unsigned long lastAccumRead;
extern bool          isAccumulating;

// ── Provisioned credentials (loaded from NVS at boot) ────────────────────────
extern ProvisionData provData;

// ── Function declarations ─────────────────────────────────────────────────────
void setup();
void loop();

// RFID
void     setupRFID();
void     resetRFIDModule();
bool     rfidHealthy();
String   printUid(const MFRC522::Uid uid);

// Machine control
void startMachineCycle();
void addCycleTime(int seconds);
void cancelCycle();
bool machineIsRunning();

// Accumulation
void commitAccumulation();

// Heartbeat
void doHeartbeat();

// Config
void applyConfig(const String& json);
void fetchConfig();

// WiFi — takes credentials from provData
void connectWifi();

// Auth
bool ensureAuthenticated();
