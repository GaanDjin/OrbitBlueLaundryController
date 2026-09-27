#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>
#include <Config.h>
#include <HTTPUpdate.h>

enum class MachineStatus : uint8_t {
    Idle    = 0,
    Running = 1,
    Error   = 2,
    OutOfOrder = 3
    // Never send Offline — that's server-side only
};


enum class MachineCommand : uint8_t {
    None         = 0,
    TryAgain     = 1,
    AddTime      = 2,
    Cancel       = 3,
    UpdateConfig = 4,
    Reboot       = 5,
    OutOfOrder   = 6,
    InOrder      = 7,
    UpdateFirmware = 8
};

struct HeartbeatResponse {
    MachineCommand command;
    String         commandPayload;  // e.g. seconds to add, or JSON config blob
    bool           ok;
};

static const int HTTP_RETRIES    = 3;
static const int RETRY_DELAY_MS  = 750;

class ApiClient {
public:
    ApiClient(const String& host, uint16_t port);

    bool login(const String& username, const String& password);
    bool refreshToken();

    bool deduct(const String& cardId, const String& accountId, float amount, const String& processorName,
                String& messageOut, float& newBalanceOut, String& displayNameOut);

    bool getBalance(const String& cardId, float& balanceOut);

    bool getServerTime(String& timeOut);

    time_t parseIso8601(const String& iso);
    
    bool needsRefresh();
    bool needsRelogin();
    void resetClient();
    HeartbeatResponse sendHeartbeat(MachineStatus status);

    bool postJson(const String& url, const String& json, String& responseOut, bool auth = false);
    bool get(const String& url, String& responseOut, bool auth = false);
    bool checkAndApplyFirmwareUpdate(const String& profile, const String& currentVersion);

    void saveCredentials();
    bool loadCredentials();
    void clearCredentials();  // returns false if nothing cached
private:
    Preferences _prefs;

    String _host;
    uint16_t _port;

    String _accessToken;
    String _refreshToken;
    time_t _accessExpiry;
    time_t _refreshExpiry;

    WiFiClientSecure _client;
    HTTPClient https;

    WiFiClient _plainClient;         // Standard TCP client for port 80 HTTP
    bool _useHttpsFallback = false;  // Latches to true if port 80 fails
};

