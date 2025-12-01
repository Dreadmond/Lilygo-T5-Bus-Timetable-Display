#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// OTA UPDATE MANAGER
// Supports both:
// 1. ElegantOTA for web-based uploads
// 2. GitHub Releases for automatic updates
// ============================================================================

class OTAUpdateManager {
public:
    OTAUpdateManager();
    
    // Initialize OTA (starts web server for ElegantOTA)
    void init();
    
    // Handle OTA loop
    void loop();
    
    // Check for GitHub updates
    bool checkForUpdate();
    
    // Perform update from GitHub
    bool performUpdate(const String& downloadUrl);
    
    // Get update status
    bool isUpdateAvailable() const;
    String getLatestVersion() const;
    String getUpdateUrl() const;
    int getUpdateProgress() const;
    bool isUpdating() const;
    
    // Set callback for update events
    void setProgressCallback(void (*callback)(int progress));
    void setCompleteCallback(void (*callback)(bool success));

private:
    bool updateAvailable;
    String latestVersion;
    String updateDownloadUrl;
    int updateProgress;
    bool updating;
    
    void (*progressCallback)(int progress);
    void (*completeCallback)(bool success);
    
    // Parse GitHub API response
    bool parseReleaseInfo(const String& jsonResponse);
    
    // Compare version strings
    bool isNewerVersion(const String& newVersion, const String& currentVersion);
    
    // Version string parsing
    int getVersionMajor(const String& version);
    int getVersionMinor(const String& version);
    int getVersionPatch(const String& version);
};

extern OTAUpdateManager otaManager;

#endif // OTA_UPDATE_H

