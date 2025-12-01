#include "ota_update.h"
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>

// ============================================================================
// OTA UPDATE MANAGER IMPLEMENTATION
// Uses ArduinoOTA for local network updates + GitHub for remote updates
// ============================================================================

OTAUpdateManager otaManager;

// Simple web server for status page
WebServer otaWebServer(80);

OTAUpdateManager::OTAUpdateManager() {
    updateAvailable = false;
    latestVersion = "";
    updateDownloadUrl = "";
    updateProgress = 0;
    updating = false;
    progressCallback = nullptr;
    completeCallback = nullptr;
}

void OTAUpdateManager::init() {
    // Setup ArduinoOTA for local network updates
    ArduinoOTA.setHostname(DEVICE_NAME);
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        DEBUG_PRINTLN("OTA Start: " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        DEBUG_PRINTLN("\nOTA End - Rebooting...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int pct = (progress / (total / 100));
        DEBUG_PRINTF("OTA Progress: %u%%\r", pct);
        if (otaManager.progressCallback) {
            otaManager.progressCallback(pct);
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        DEBUG_PRINTF("OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) DEBUG_PRINTLN("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) DEBUG_PRINTLN("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
        else if (error == OTA_END_ERROR) DEBUG_PRINTLN("End Failed");
        
        if (otaManager.completeCallback) {
            otaManager.completeCallback(false);
        }
    });
    
    ArduinoOTA.begin();
    
    // Setup simple web server for status/info
    otaWebServer.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>Bus Timetable Display</title>";
        html += "<style>";
        html += "body{font-family:system-ui,-apple-system,sans-serif;background:#1a1a1a;color:#fff;margin:0;padding:40px;text-align:center;}";
        html += "h1{color:#FFB81C;font-size:2em;margin-bottom:10px;}";
        html += ".card{background:#2a2a2a;border-radius:15px;padding:30px;max-width:400px;margin:20px auto;}";
        html += ".info{color:#888;margin:10px 0;}";
        html += ".value{color:#fff;font-size:1.2em;font-weight:bold;}";
        html += ".btn{background:#FFB81C;color:#1a1a1a;border:none;padding:15px 30px;border-radius:8px;font-size:1em;cursor:pointer;margin-top:20px;}";
        html += ".btn:hover{background:#ffc94d;}";
        html += "</style></head><body>";
        html += "<h1>🚌 Bus Timetable</h1>";
        html += "<p style='color:#888;'>E-Ink Display</p>";
        html += "<div class='card'>";
        html += "<div class='info'>Version</div><div class='value'>v" + String(FIRMWARE_VERSION) + "</div>";
        html += "<div class='info'>IP Address</div><div class='value'>" + WiFi.localIP().toString() + "</div>";
        html += "<div class='info'>WiFi Signal</div><div class='value'>" + String(WiFi.RSSI()) + " dBm</div>";
        html += "<div class='info'>Uptime</div><div class='value'>" + String(millis() / 60000) + " min</div>";
        html += "<div class='info'>Free Heap</div><div class='value'>" + String(ESP.getFreeHeap() / 1024) + " KB</div>";
        html += "</div>";
        html += "<div class='card'>";
        html += "<div class='info'>OTA Update</div>";
        html += "<div class='value'>Use PlatformIO or Arduino IDE</div>";
        html += "<p style='color:#666;font-size:0.9em;margin-top:15px;'>Device: " + String(DEVICE_NAME) + "</p>";
        html += "</div>";
        html += "</body></html>";
        otaWebServer.send(200, "text/html", html);
    });
    
    otaWebServer.on("/api/info", HTTP_GET, []() {
        String json = "{";
        json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
        json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
        json += "\"uptime\":" + String(millis() / 1000) + ",";
        json += "\"heap_free\":" + String(ESP.getFreeHeap());
        json += "}";
        otaWebServer.send(200, "application/json", json);
    });
    
    otaWebServer.on("/reboot", HTTP_GET, []() {
        otaWebServer.send(200, "text/plain", "Rebooting...");
        delay(500);
        ESP.restart();
    });
    
    otaWebServer.begin();
    
    DEBUG_PRINTLN("OTA initialized");
    DEBUG_PRINTF("Web server: http://%s/\n", WiFi.localIP().toString().c_str());
    DEBUG_PRINTF("OTA hostname: %s\n", DEVICE_NAME);
}

void OTAUpdateManager::loop() {
    ArduinoOTA.handle();
    otaWebServer.handleClient();
}

bool OTAUpdateManager::checkForUpdate() {
    DEBUG_PRINTLN("Checking for updates on GitHub...");
    
    WiFiClientSecure client;
    client.setInsecure(); // Skip certificate verification
    
    HTTPClient http;
    
    String url = "https://api.github.com/repos/" + 
                 String(OTA_GITHUB_USER) + "/" + 
                 String(OTA_GITHUB_REPO) + "/releases/latest";
    
    http.begin(client, url);
    http.addHeader("Accept", "application/vnd.github.v3+json");
    http.addHeader("User-Agent", "ESP32-OTA");
    http.setTimeout(10000);
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        http.end();
        return parseReleaseInfo(response);
    }
    
    DEBUG_PRINTF("GitHub API error: %d\n", httpCode);
    http.end();
    return false;
}

bool OTAUpdateManager::parseReleaseInfo(const String& jsonResponse) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (error) {
        DEBUG_PRINTF("JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    String tagName = doc["tag_name"].as<String>();
    
    // Remove 'v' prefix if present
    if (tagName.startsWith("v") || tagName.startsWith("V")) {
        tagName = tagName.substring(1);
    }
    
    latestVersion = tagName;
    
    // Look for .bin asset
    JsonArray assets = doc["assets"];
    for (JsonObject asset : assets) {
        String name = asset["name"].as<String>();
        if (name.endsWith(".bin")) {
            updateDownloadUrl = asset["browser_download_url"].as<String>();
            break;
        }
    }
    
    if (updateDownloadUrl.length() == 0) {
        DEBUG_PRINTLN("No .bin asset found in release");
        return false;
    }
    
    updateAvailable = isNewerVersion(latestVersion, FIRMWARE_VERSION);
    
    DEBUG_PRINTF("Latest version: %s, Current: %s, Update available: %s\n",
                 latestVersion.c_str(), FIRMWARE_VERSION, 
                 updateAvailable ? "Yes" : "No");
    
    return updateAvailable;
}

bool OTAUpdateManager::performUpdate(const String& downloadUrl) {
    if (updating) return false;
    
    DEBUG_PRINTLN("Starting firmware update...");
    updating = true;
    updateProgress = 0;
    
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, downloadUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    
    int httpCode = http.GET();
    
    if (httpCode != HTTP_CODE_OK) {
        DEBUG_PRINTF("Download failed: %d\n", httpCode);
        http.end();
        updating = false;
        if (completeCallback) completeCallback(false);
        return false;
    }
    
    int contentLength = http.getSize();
    if (contentLength <= 0) {
        DEBUG_PRINTLN("Invalid content length");
        http.end();
        updating = false;
        if (completeCallback) completeCallback(false);
        return false;
    }
    
    DEBUG_PRINTF("Firmware size: %d bytes\n", contentLength);
    
    if (!Update.begin(contentLength)) {
        DEBUG_PRINTLN("Not enough space for update");
        http.end();
        updating = false;
        if (completeCallback) completeCallback(false);
        return false;
    }
    
    WiFiClient* stream = http.getStreamPtr();
    
    uint8_t buff[1024];
    int written = 0;
    
    while (http.connected() && written < contentLength) {
        size_t available = stream->available();
        if (available) {
            int readBytes = stream->readBytes(buff, min(available, sizeof(buff)));
            int writtenBytes = Update.write(buff, readBytes);
            
            if (writtenBytes != readBytes) {
                DEBUG_PRINTLN("Write error");
                break;
            }
            
            written += writtenBytes;
            updateProgress = (written * 100) / contentLength;
            
            if (progressCallback) {
                progressCallback(updateProgress);
            }
        }
        delay(1);
    }
    
    http.end();
    
    if (Update.end()) {
        if (Update.isFinished()) {
            DEBUG_PRINTLN("Update successful! Restarting...");
            updating = false;
            if (completeCallback) completeCallback(true);
            delay(1000);
            ESP.restart();
            return true;
        }
    }
    
    DEBUG_PRINTF("Update failed: %s\n", Update.errorString());
    updating = false;
    if (completeCallback) completeCallback(false);
    return false;
}

bool OTAUpdateManager::isUpdateAvailable() const {
    return updateAvailable;
}

String OTAUpdateManager::getLatestVersion() const {
    return latestVersion;
}

String OTAUpdateManager::getUpdateUrl() const {
    return updateDownloadUrl;
}

int OTAUpdateManager::getUpdateProgress() const {
    return updateProgress;
}

bool OTAUpdateManager::isUpdating() const {
    return updating;
}

void OTAUpdateManager::setProgressCallback(void (*callback)(int progress)) {
    progressCallback = callback;
}

void OTAUpdateManager::setCompleteCallback(void (*callback)(bool success)) {
    completeCallback = callback;
}

bool OTAUpdateManager::isNewerVersion(const String& newVersion, const String& currentVersion) {
    int newMajor = getVersionMajor(newVersion);
    int newMinor = getVersionMinor(newVersion);
    int newPatch = getVersionPatch(newVersion);
    
    int curMajor = getVersionMajor(currentVersion);
    int curMinor = getVersionMinor(currentVersion);
    int curPatch = getVersionPatch(currentVersion);
    
    if (newMajor > curMajor) return true;
    if (newMajor < curMajor) return false;
    
    if (newMinor > curMinor) return true;
    if (newMinor < curMinor) return false;
    
    return newPatch > curPatch;
}

int OTAUpdateManager::getVersionMajor(const String& version) {
    int firstDot = version.indexOf('.');
    if (firstDot < 0) return version.toInt();
    return version.substring(0, firstDot).toInt();
}

int OTAUpdateManager::getVersionMinor(const String& version) {
    int firstDot = version.indexOf('.');
    if (firstDot < 0) return 0;
    int secondDot = version.indexOf('.', firstDot + 1);
    if (secondDot < 0) return version.substring(firstDot + 1).toInt();
    return version.substring(firstDot + 1, secondDot).toInt();
}

int OTAUpdateManager::getVersionPatch(const String& version) {
    int firstDot = version.indexOf('.');
    if (firstDot < 0) return 0;
    int secondDot = version.indexOf('.', firstDot + 1);
    if (secondDot < 0) return 0;
    return version.substring(secondDot + 1).toInt();
}
