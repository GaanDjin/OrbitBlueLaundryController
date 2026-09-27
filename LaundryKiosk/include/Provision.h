#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>

// ── Provisioning ──────────────────────────────────────────────────────────────
// On first boot (or after CLEAR_PROVISION), the device starts a WiFi AP
// and serves a web form to collect credentials. Once saved to NVS, it reboots
// and connects normally. This allows a single firmware binary per profile
// (dryer/washer) without hardcoded credentials.
//
// Connect to AP: "OrbitSetup-XXXXXX" (last 6 of MAC)
// Browse to:     http://192.168.4.1
//
// If WiFi fails after provisioning, clearProvisionData() and reboot back here.
// ─────────────────────────────────────────────────────────────────────────────

struct ProvisionData {
    String wifiSsid;
    String wifiPass;
    String apiHost;
    uint16_t apiPort  = API_PORT;
    String apiUser;
    String apiPass;
    String machineName;
    bool   provisioned = false;
    bool   verified    = false;  // true after first successful login
};

// Load credentials from NVS — returns false if not provisioned yet
inline bool loadProvisionData(ProvisionData& out) {
    Preferences prefs;
    prefs.begin("provision", true);
    out.provisioned = prefs.getBool("ok",       false);
    out.verified    = prefs.getBool("verified",  false);
    out.wifiSsid    = prefs.getString("ssid",  "");
    out.wifiPass    = prefs.getString("wpass", "");
    out.apiHost     = prefs.getString("host",  API_HOST);
    out.apiPort     = (uint16_t)prefs.getUInt("port",  API_PORT);
    out.apiUser     = prefs.getString("user",  "");
    out.apiPass     = prefs.getString("pass",  "");
    out.machineName = prefs.getString("name",  "");
    prefs.end();
    return out.provisioned && out.wifiSsid.length() > 0 && out.apiUser.length() > 0;
}

inline void clearProvisionData() {
    Preferences prefs;
    prefs.begin("provision", false);
    prefs.clear();
    prefs.end();
    Serial.println("[Provision] Cleared.");
}

// Call after first successful login to mark these credentials as known-good
inline void markProvisionVerified() {
    Preferences prefs;
    prefs.begin("provision", false);
    prefs.putBool("verified", true);
    prefs.end();
    Serial.println("[Provision] Marked as verified.");
}

// Run the provisioning AP + web form — blocks until provisioned then reboots
inline void runProvisioningMode() {
    // Unique AP name using MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char apName[32];
    snprintf(apName, sizeof(apName), "OrbitSetup-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    Serial.printf("[Provision] Starting AP: %s\n", apName);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName, "orbitsetup");
    IPAddress ip = WiFi.softAPIP();
    Serial.print("[Provision] AP IP: "); Serial.println(ip);

    WebServer server(80);
    bool done = false;

    // ── Serve the form ──
    server.on("/", HTTP_GET, [&]() {
        server.send(200, "text/html", R"(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>
  body{font-family:monospace;background:#0f1117;color:#d4d8e8;
       display:flex;align-items:center;justify-content:center;min-height:100vh;margin:0;}
  .box{background:#181c25;border:1px solid #2a2f42;border-radius:8px;
       padding:32px;width:320px;}
  h2{color:#4f8ef7;letter-spacing:4px;margin:0 0 24px;}
  label{font-size:11px;letter-spacing:1px;text-transform:uppercase;
        color:#4a5068;display:block;margin-bottom:4px;}
  input{width:100%;padding:8px 10px;background:#1f2333;border:1px solid #2a2f42;
        border-radius:4px;color:#d4d8e8;font-family:monospace;font-size:13px;
        box-sizing:border-box;margin-bottom:16px;}
  button{width:100%;padding:10px;background:#4f8ef7;color:#fff;border:none;
         border-radius:4px;font-family:monospace;font-size:13px;cursor:pointer;}
  .section{font-size:10px;letter-spacing:2px;text-transform:uppercase;
           color:#4a5068;margin:16px 0 12px;padding-bottom:6px;
           border-bottom:1px solid #2a2f42;}
  .row{display:flex;gap:8px;}
  .row input{margin-bottom:16px;}
  .grow{flex:1;}
  .shrink{width:80px;flex-shrink:0;}
</style></head><body><div class='box'>
<h2>ORBIT</h2>
<form method='POST' action='/save'>
  <div class='section'>WiFi</div>
  <label>SSID</label><input name='ssid' required>
  <label>Password</label><input name='wpass' type='password'>
  <div class='section'>API</div>
  <div class='row'>
    <div class='grow'><label>Host</label><input name='host' value=')" + String(API_HOST) + R"(' required></div>
    <div class='shrink'><label>Port</label><input name='port' type='number' value=')" + String(API_PORT) + R"(' required></div>
  </div>
  <label>Username</label><input name='user' required>
  <label>Password</label><input name='pass' type='password' required>
  <div class='section'>Machine</div>
  <label>Display Name</label><input name='name' placeholder='Dryer 1'>
  <button type='submit'>Save &amp; Connect</button>
</form></div></body></html>
        )");
    });

    // ── Handle form submission ──
    server.on("/save", HTTP_POST, [&]() {
        String ssid  = server.arg("ssid");
        String wpass = server.arg("wpass");
        String host  = server.arg("host");
        String port  = server.arg("port");
        String user  = server.arg("user");
        String pass  = server.arg("pass");
        String name  = server.arg("name");

        if (ssid.isEmpty() || user.isEmpty() || pass.isEmpty() || host.isEmpty()) {
            server.send(400, "text/html",
                "<h2 style='font-family:monospace;color:#e05252;padding:20px;'>"
                "Missing required fields.</h2>");
            return;
        }

        // Save to NVS
        Preferences prefs;
        prefs.begin("provision", false);
        prefs.putString("ssid",  ssid);
        prefs.putString("wpass", wpass);
        prefs.putString("host",  host);
        prefs.putUInt("port",    (uint32_t)port.toInt());
        prefs.putString("user",  user);
        prefs.putString("pass",  pass);
        prefs.putString("name",  name);
        prefs.putBool("ok",      true);
        prefs.end();

        // Clear any cached JWT so the new credentials are validated fresh on boot
        Preferences apiPrefs;
        apiPrefs.begin("apiclient", false);
        apiPrefs.clear();
        apiPrefs.end();
        Serial.println("[Provision] JWT cache cleared.");

        Serial.println("[Provision] Saved. Rebooting...");

        server.send(200, "text/html", R"(
<!DOCTYPE html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<style>body{font-family:monospace;background:#0f1117;color:#3ecf8e;
     display:flex;align-items:center;justify-content:center;min-height:100vh;}</style>
</head><body>
<div style='text-align:center'>
  <div style='font-size:32px;margin-bottom:16px;'>✓</div>
  <div style='letter-spacing:2px;'>PROVISIONED</div>
  <div style='color:#4a5068;margin-top:8px;font-size:12px;'>Rebooting...</div>
</div></body></html>
        )");

        done = true;
    });

    server.begin();

    // Block until provisioned
    while (!done) {
        server.handleClient();
        delay(10);
    }

    delay(1500);
    ESP.restart();
}
