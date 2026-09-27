#include "ApiClient.h"

ApiClient::ApiClient(const String& host, uint16_t port)
    : _host(host), _port(port)
{
    configTime(0, 0, "pool.ntp.org");  // your firewall will intercept this
    _client.setInsecure();   // Accept all HTTPS certificates for now
    https.setReuse(true);

    #if defined(CLEAR_CLIENT_PREFS)
        _prefs.begin("apiclient", false);
        _prefs.clear();
        _prefs.end();
        Serial.println("[Auth] Credentials cleared.");
    #endif
}

// Returns true if the HTTPClient error code indicates a transport/TLS failure
// (as opposed to a legitimate HTTP error like 401/404)
static bool isTlsError(int code) {
    return code == HTTPC_ERROR_CONNECTION_REFUSED  ||
           code == HTTPC_ERROR_CONNECTION_LOST     ||
           code == HTTPC_ERROR_SEND_HEADER_FAILED  ||
           code == HTTPC_ERROR_SEND_PAYLOAD_FAILED ||
           code == HTTPC_ERROR_NOT_CONNECTED       ||
           code == HTTPC_ERROR_READ_TIMEOUT        ||
           code <= -29000;  // mbedTLS error range (e.g. -29312 EOF, -1 generic)
}

// ── Hard reset the underlying TCP+TLS client ──────────────────────────────────
// Call this when the connection is in an unknown or errored state.
// After stop(), the next begin() will do a full TCP connect + TLS handshake.
void ApiClient::resetClient() {
    https.end();
    _client.stop();
    delay(200);
    Serial.println("[ApiClient] Client reset.");
}


// ── POST ──────────────────────────────────────────────────────────────────────

bool ApiClient::postJson(const String& url, const String& json,
                         String& responseOut, bool auth)
{
    String fullUrl = "https://" + _host + ":" + String(_port) + url;

    for (int attempt = 1; attempt <= HTTP_RETRIES; attempt++) {

        if (!https.begin(_client, fullUrl)) {
            Serial.printf("[POST] https.begin() failed (attempt %d): %s\n",
                          attempt, url.c_str());
            resetClient();
            delay(RETRY_DELAY_MS * attempt);
            continue;
        }

        https.addHeader("Content-Type", "application/json");
        if (auth && _accessToken.length() > 0)
            https.addHeader("Authorization", "Bearer " + _accessToken);

        int code = https.POST(json);

        if (code > 0) {
            // Got a real HTTP response — success or server-side error, either way
            // the transport worked. Return the body and let the caller decide.
            responseOut = https.getString();
            https.end();
            if (attempt > 1)
                Serial.printf("[POST] Succeeded on attempt %d\n", attempt);
            return code >= 200 && code < 300;
        }

        // Transport / TLS failure
        responseOut = https.errorToString(code);
        Serial.printf("[POST] Transport error %d (%s) on attempt %d: %s\n",
                      code, responseOut.c_str(), attempt, url.c_str());
        resetClient();
        delay(RETRY_DELAY_MS * attempt);
    }

    Serial.printf("[POST] All %d attempts failed: %s\n", HTTP_RETRIES, url.c_str());
    return false;
}


// ── GET ───────────────────────────────────────────────────────────────────────

bool ApiClient::get(const String& url, String& responseOut, bool auth)
{
    String fullUrl = "https://" + _host + ":" + String(_port) + url;

    for (int attempt = 1; attempt <= HTTP_RETRIES; attempt++) {

        if (!https.begin(_client, fullUrl)) {
            Serial.printf("[GET] https.begin() failed (attempt %d): %s\n",
                          attempt, url.c_str());
            resetClient();
            delay(RETRY_DELAY_MS * attempt);
            continue;
        }

        if (auth && _accessToken.length() > 0)
            https.addHeader("Authorization", "Bearer " + _accessToken);

        int code = https.GET();

        if (code > 0) {
            responseOut = https.getString();
            if (attempt > 1)
                Serial.printf("[GET] Succeeded on attempt %d\n", attempt);
            if (code < 200 || code >= 300)
                Serial.printf("[GET] HTTP %d: %s\n", code, url.c_str());
            https.end();
            return code >= 200 && code < 300;
        }

        responseOut = https.errorToString(code);
        Serial.printf("[GET] Transport error %d (%s) on attempt %d: %s\n",
                      code, responseOut.c_str(), attempt, url.c_str());
        resetClient();
        delay(RETRY_DELAY_MS * attempt);
    }

    Serial.printf("[GET] All %d attempts failed: %s\n", HTTP_RETRIES, url.c_str());
    return false;
}

bool ApiClient::login(const String& username, const String& password)
{
    String body = "{\"username\":\"" + username + "\",\"password\":\"" + password + "\"}";
    String response;

    if (!postJson("/Login", body, response, false)) {
        Serial.print("Request failed:");
        Serial.println("/Login");
        Serial.println(response);
        return false;
    }

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, response)) return false;

    _accessToken  = doc["accessToken"].as<String>();
    _refreshToken = doc["refreshToken"].as<String>();

    String accessExp  = doc["accessExpiresUtc"].as<String>();
    String refreshExp = doc["refreshExpiresUtc"].as<String>();

    _accessExpiry  = parseIso8601(accessExp);
    _refreshExpiry = parseIso8601(refreshExp);

    saveCredentials();   // ← add this
    return _accessToken.length() > 0;
}

time_t timegm(struct tm *t) {
    // Save timezone
    time_t local = mktime(t);
    struct tm temp;
    gmtime_r(&local, &temp);

    // Difference between local and UTC
    time_t diff = local - mktime(&temp);

    return local + diff;
}

time_t ApiClient::parseIso8601(const String& iso) {
    struct tm t = {0};

    // Example: 2026-02-28T20:43:46.8152824+00:00
    //          YYYY-MM-DDTHH:MM:SS

    t.tm_year = iso.substring(0, 4).toInt() - 1900;
    t.tm_mon  = iso.substring(5, 7).toInt() - 1;
    t.tm_mday = iso.substring(8, 10).toInt();
    t.tm_hour = iso.substring(11, 13).toInt();
    t.tm_min  = iso.substring(14, 16).toInt();
    t.tm_sec  = iso.substring(17, 19).toInt();

    // Convert to UTC time_t
    time_t utc = timegm(&t);  // ESP32 supports timegm()

    return utc;
}


bool ApiClient::refreshToken()
{
    String body = "\"" + _refreshToken + "\"";
    String response;

    if (!postJson("/refresh", body, response, false)) {
        Serial.print("Request failed:");
        Serial.println("/refresh");
        Serial.println(response);
        return false;
    }

    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, response)) return false;

    _accessToken  = doc["accessToken"].as<String>();
    _refreshToken = doc["refreshToken"].as<String>();

    String accessExp  = doc["accessExpiresUtc"].as<String>();
    String refreshExp = doc["refreshExpiresUtc"].as<String>();

    _accessExpiry  = parseIso8601(accessExp);
    _refreshExpiry = parseIso8601(refreshExp);

    saveCredentials();   // ← add this
    return true;
}

bool ApiClient::needsRefresh() {
    time_t now = time(nullptr);
    return now >= (_accessExpiry - 300);  // refresh 5 minutes early
}

bool ApiClient::needsRelogin() {
    time_t now = time(nullptr);
    return now >= _refreshExpiry;
}

void ApiClient::saveCredentials() {
    _prefs.begin("apiclient", false);
    _prefs.putString("access",       _accessToken);
    _prefs.putString("refresh",      _refreshToken);
    _prefs.putLong("access_exp",    (long)_accessExpiry);
    _prefs.putLong("refresh_exp",   (long)_refreshExpiry);
    _prefs.end();
    Serial.println("[Auth] Credentials saved to NVS.");
}

bool ApiClient::loadCredentials() {
    _prefs.begin("apiclient", true);  // read-only
    _accessToken   = _prefs.getString("access",  "");
    _refreshToken  = _prefs.getString("refresh", "");
    _accessExpiry  = (time_t)_prefs.getLong("access_exp",  0);
    _refreshExpiry = (time_t)_prefs.getLong("refresh_exp", 0);
    _prefs.end();

    if (_accessToken.isEmpty() || _refreshExpiry == 0) {
        Serial.println("[Auth] No cached credentials.");
        return false;
    }

    Serial.println("[Auth] Loaded cached credentials.");
    return true;
}


bool ApiClient::refund(const String& cardId, const String& accountId, float amount,
                       const String& processorName, String& messageOut,
                       float& newBalanceOut, String& displayNameOut)
{
    String body = "{";
    body += "\"cardId\":\"" + cardId + "\",";
    //body += "\"accountId\":\"" + accountId + "\",";
    body += "\"amount\":" + String(amount, 2) + ",";
    body += "\"processorName\":\"" + processorName + "\"";
    body += "}";

    String response;
    if (!postJson("/tap/refund", body, response, true)) {
        Serial.print("Request failed:");
        Serial.println("/tap/deduct");
        Serial.println(response);
        return false;
    }

    Serial.println("[isAdmin] Response: " + response);
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, response)) return false;

    messageOut      = doc["message"].as<String>();
    newBalanceOut   = doc["newBalance"].as<float>();
    displayNameOut  = doc["accountDisplayName"].as<String>();

    return true;
}

bool ApiClient::deduct(const String& cardId, const String& accountId, float amount,
                       const String& processorName, String& messageOut,
                       float& newBalanceOut, String& displayNameOut)
{
    String body = "{";
    body += "\"cardId\":\"" + cardId + "\",";
    //body += "\"accountId\":\"" + accountId + "\",";
    body += "\"amount\":" + String(amount, 2) + ",";
    body += "\"processorName\":\"" + processorName + "\"";
    body += "}";

    String response;
    if (!postJson("/tap/deduct", body, response, true)) {
        Serial.print("Request failed:");
        Serial.println("/tap/deduct");
        Serial.println(response);
        return false;
    }

    Serial.println("[isAdmin] Response: " + response);
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, response)) return false;

    messageOut      = doc["message"].as<String>();
    newBalanceOut   = doc["newBalance"].as<float>();
    displayNameOut  = doc["accountDisplayName"].as<String>();

    return true;
}

bool ApiClient::getBalance(const String& cardId, float& balanceOut)
{
    String response;
    String requestUrl = "/tap/" + cardId + "/balance";
    if (!get(requestUrl, response, true)) {
        Serial.print("Request failed:");
        Serial.println(requestUrl);
        Serial.println(response);

        return false;
    }

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, response)) return false;

    balanceOut = doc["balance"].as<float>();
    return true;
}

bool ApiClient::isAdmin(const String& cardId){
    String response;
    String requestUrl = "/cards/list?Filters[CardId]=" + cardId;
    if (!get(requestUrl, response, true)) {
        Serial.println("[isAdmin] Request failed: " + requestUrl);
        Serial.println("[isAdmin] Response: " + response);
        return false;
    }

    Serial.println("[isAdmin] Response: " + response);

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, response)) {
        Serial.println("[isAdmin] JSON parse failed");
        return false;
    }

    if (!doc["rows"].is<JsonArray>() || doc["rows"].size() == 0) {
        Serial.println("[isAdmin] No rows in response");
        return false;
    }

    bool isAdmin = doc["rows"][0]["isAdminCard"].as<bool>();
    return isAdmin;
}

bool ApiClient::addFunds(const String& cardId, float amount, float& newBalanceOut)
{
    String body = "{";
    body += "\"cardId\":\"" + cardId + "\",";
    body += "\"amount\":" + String(amount, 2);
    body += "}";

    String response;
    if (!postJson("/tap/add", body, response, true)) {
        Serial.println("[Admin] addFunds request failed.");
        Serial.println(response);
        return false;
    }

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, response)) return false;

    newBalanceOut = doc["newBalance"].as<float>();
    return true;
}


bool ApiClient::getServerTime(String& timeOut)
{
    String response;
    if (!get("/time", response, false)) {
        Serial.print("Request failed:");
        Serial.println("/time");
        Serial.println(response);
        return false;
    }

    timeOut = response;
    return true;
}

// ApiClient.cpp

HeartbeatResponse ApiClient::sendHeartbeat(MachineStatus status)
{
    HeartbeatResponse result { MachineCommand::None, "", false };

    // Map enum to the string your server expects
    const char* statusStr = "Idle";
    switch (status) {
        case MachineStatus::Running:    statusStr = "Running";    break;
        case MachineStatus::Error:      statusStr = "Error";      break;
        case MachineStatus::OutOfOrder: statusStr = "OutOfOrder"; break;
        default:                        statusStr = "Idle";       break;
    }

    String body = "{\"status\":\"";
    body += statusStr;
    body += "\"}";

    String response;
    if (!postJson("/machines/heartbeat", body, response, true)) {
        Serial.println("[Heartbeat] Request failed");
        Serial.println(response);
        return result;
    }

    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, response)) {
        Serial.println("[Heartbeat] JSON parse failed");
        return result;
    }

// Parse the command back from the server
String cmdStr = doc["command"] | "None";

    if      (cmdStr == "TryAgain")       result.command = MachineCommand::TryAgain;
    else if (cmdStr == "AddTime")        result.command = MachineCommand::AddTime;
    else if (cmdStr == "Cancel")         result.command = MachineCommand::Cancel;
    else if (cmdStr == "UpdateConfig")   result.command = MachineCommand::UpdateConfig;
    else if (cmdStr == "Reboot")         result.command = MachineCommand::Reboot;
    else if (cmdStr == "OutOfOrder")     result.command = MachineCommand::OutOfOrder;
    else if (cmdStr == "InOrder")        result.command = MachineCommand::InOrder;
    else if (cmdStr == "UpdateFirmware") result.command = MachineCommand::UpdateFirmware;
    else                                 result.command = MachineCommand::None;

    result.commandPayload = doc["commandPayload"] | "";
    result.ok             = true;

    if (result.command != MachineCommand::None)
        Serial.println("[Heartbeat] Command received: " + cmdStr);

    return result;
}

// ── OTA firmware update ──────────────────────────────────────────────────────
void ApiClient::clearCredentials() {
    _accessToken  = "";
    _refreshToken = "";
    _accessExpiry  = 0;
    _refreshExpiry = 0;
    _prefs.begin("apiclient", false);
    _prefs.clear();
    _prefs.end();
    Serial.println("[Auth] Credentials cleared.");
}

// ── OTA firmware update ───────────────────────────────────────────────────────
bool ApiClient::checkAndApplyFirmwareUpdate(const String& profile, const String& currentVersion)
{
    // Step 1: Check if a newer version is available (authenticated)
    String response;
    if (!get("/firmware/version?profile=" + profile, response, false)) {
        Serial.println("[OTA] Version check failed.");
        return false;
    }

    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, response)) {
        Serial.println("[OTA] Version parse failed.");
        return false;
    }

    String serverVersion = doc["version"] | "";
    if (serverVersion.isEmpty() || serverVersion == currentVersion) {
        Serial.println("[OTA] Firmware up to date: " + currentVersion);
        return false;
    }

    Serial.println("[OTA] Update available: " + serverVersion + " (current: " + currentVersion + ")");

    // Step 2: Download and flash
    String url = "https://" + _host + ":" + String(_port)
               + "/firmware/download?profile=" + profile;

    Serial.println("[OTA] Downloading from: " + url);

    WiFiClientSecure otaClient;
    otaClient.setInsecure();

    httpUpdate.rebootOnUpdate(true);

    t_httpUpdate_return ret = httpUpdate.update(otaClient, url);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Failed (%d): %s\n",
                httpUpdate.getLastError(),
                httpUpdate.getLastErrorString().c_str());
            return false;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No update needed.");
            return false;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] Update OK.");
            return true;  // rebootOnUpdate=true so this may not be reached
    }
    return false;
}