/**
 * ============================================================================
 * BUS TIMETABLE E-INK DISPLAY
 * For LilyGo T5 4.7" (960x540 pixels)
 * ============================================================================
 * 
 * A beautiful, high-contrast bus departure display designed for
 * elderly users with large fonts and clear visual hierarchy.
 * 
 * Features:
 * - Live bus departures from TransportAPI
 * - Walking time to stops calculated
 * - Home Assistant integration via MQTT auto-discovery
 * - OTA updates via web interface or GitHub releases
 * - Battery monitoring and reporting
 * 
 * Author: Matt
 * License: MIT
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"
#include "display.h"
#include "transport_api.h"
#include "mqtt_ha.h"
#include "ota_update.h"

// WiFi configuration portal
Preferences wifiPrefs;
WebServer configServer(80);
DNSServer dnsServer;
bool configPortalActive = false;

// ============================================================================
// GLOBAL STATE
// ============================================================================

// Timing variables
unsigned long lastBusUpdate = 0;
unsigned long lastMqttPublish = 0;
unsigned long lastBatteryRead = 0;
unsigned long lastOtaCheck = 0;
unsigned long lastDisplayRefresh = 0;
unsigned long lastCountdownUpdate = 0;
unsigned long lastFooterUpdate = 0;
unsigned long lastDataFetch = 0;  // When we last got fresh data

// Battery state
int batteryPercent = 100;
float batteryVoltage = 4.2;

// Bus data
BusDeparture departures[20];  // Buffer for collecting from all stops before filtering
int departureCount = 0;

// Connection state
bool wifiConnected = false;
bool mqttConnected = false;
bool showingPlaceholderData = false;
bool sleepModeActive = false;

// Current time string
String currentTimeStr = "--:--";

// ============================================================================
// FUNCTION PROTOTYPES
// ============================================================================

void setupWiFi();
void setupTime();
void updateCurrentTime();
void readBattery();
void fetchAndDisplayBuses();
void publishMqttState();
void handleMqttCommand(const String& command);
void populatePlaceholderDepartures(const String& reason);
void handleDisplayTick(unsigned long now);
void decrementDepartureCountdowns(unsigned long minutesElapsed);
String formatFutureTime(int minutesAhead);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    // Initialize serial for debugging
    Serial.begin(DEBUG_BAUD_RATE);
    delay(1000);
    
    DEBUG_PRINTLN("\n\n");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTLN("  BUS TIMETABLE E-INK DISPLAY");
    DEBUG_PRINTLN("  LilyGo T5 4.7\" Edition");
    DEBUG_PRINTF("  Version: %s\n", FIRMWARE_VERSION);
    DEBUG_PRINTLN("========================================\n");
    
    // Initialize display first for visual feedback
    DEBUG_PRINTLN("Initializing display...");
    display.init();
    display.clear();  // Full reset to remove ghosting from previous content
    display.showLoading("Starting up...");
    
    // Setup WiFi
    DEBUG_PRINTLN("Connecting to WiFi...");
    display.showLoading("Connecting to WiFi...");
    setupWiFi();
    
    if (wifiConnected) {
        // Setup time synchronization
        DEBUG_PRINTLN("Synchronizing time...");
        display.showLoading("Syncing time...");
        setupTime();
        updateCurrentTime();
        
        // Initialize transport API
        DEBUG_PRINTLN("Initializing Transport API...");
        transportApi.init();
        
        // Initialize MQTT
        DEBUG_PRINTLN("Initializing MQTT...");
        display.showLoading("Connecting to MQTT...");
        mqtt.init();
        mqtt.setCommandCallback(handleMqttCommand);
        mqtt.connect();
        
        // Initialize OTA
        DEBUG_PRINTLN("Initializing OTA...");
        otaManager.init();
        
        // Check for OTA updates on startup
        DEBUG_PRINTLN("Checking for firmware updates...");
        if (otaManager.checkForUpdate()) {
            DEBUG_PRINTLN("Update available! Installing...");
            display.showLoading("Installing update...");
            otaManager.performUpdate(otaManager.getUpdateUrl());
            // If we get here, update failed
            display.showError("Update failed");
            delay(3000);
        }
        
        // Initial battery read
        readBattery();
        
        // Fetch initial bus data
        DEBUG_PRINTLN("Fetching initial bus data...");
        display.showLoading("Loading bus times...");
        if (transportApi.isActiveHours()) {
            fetchAndDisplayBuses();
        } else {
            populatePlaceholderDepartures("Sleeping 21:00-06:00");
            display.showBusTimetable(departures, departureCount,
                                     currentTimeStr, transportApi.getDirectionLabel(),
                                     batteryPercent, wifiConnected, showingPlaceholderData);
            unsigned long now = millis();
            lastCountdownUpdate = now;
            lastDisplayRefresh = now;
            sleepModeActive = true;
        }
    } else {
        display.showError("WiFi connection failed");
    }
    
    DEBUG_PRINTLN("\nSetup complete!\n");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    // Handle WiFi config portal if active
    if (configPortalActive) {
        dnsServer.processNextRequest();
        configServer.handleClient();
        delay(10);
        return;  // Don't run normal loop while in config mode
    }
    
    unsigned long now = millis();
    
    // Update current time string
    updateCurrentTime();
    bool activeHours = transportApi.isActiveHours();
    
    if (!activeHours && !sleepModeActive) {
        populatePlaceholderDepartures("Sleeping 21:00-06:00");
        display.showBusTimetable(departures, departureCount,
                                  currentTimeStr, transportApi.getDirectionLabel(),
                                  batteryPercent, wifiConnected, showingPlaceholderData);
        lastCountdownUpdate = now;
        lastDisplayRefresh = now;
        sleepModeActive = true;
    } else if (activeHours && sleepModeActive) {
        sleepModeActive = false;
        lastBusUpdate = 0; // Force refresh when coming back online
    }
    
    // Handle MQTT
    if (wifiConnected) {
        mqtt.loop();
        mqttConnected = mqtt.isConnected();
    }
    
    // Handle OTA
    otaManager.loop();
    
    // Determine refresh interval based on active hours
    unsigned long refreshInterval = BUS_DATA_REFRESH_INTERVAL_MS;
    
    // Refresh bus data
    if (wifiConnected && activeHours && (now - lastBusUpdate >= refreshInterval)) {
        DEBUG_PRINTLN("Refreshing bus data...");
        fetchAndDisplayBuses();
        lastBusUpdate = now;
    }
    
    // Read battery
    if (now - lastBatteryRead >= BATTERY_READ_INTERVAL_MS) {
        readBattery();
        lastBatteryRead = now;
    }
    
    // Publish MQTT state every minute
    if (mqttConnected && (now - lastMqttPublish >= 60000)) {
        publishMqttState();
        lastMqttPublish = now;
    }
    
    // Check for OTA updates hourly
    if (wifiConnected && (now - lastOtaCheck >= OTA_CHECK_INTERVAL_MS)) {
        if (otaManager.checkForUpdate()) {
            DEBUG_PRINTLN("Update available! Performing update...");
            otaManager.performUpdate(otaManager.getUpdateUrl());
        }
        lastOtaCheck = now;
    }
    
    // Update display between API refreshes
    handleDisplayTick(now);
    
    // Low battery warning
    if (batteryPercent < 10 && batteryPercent > 0) {
        display.showLowBattery(batteryPercent);
        // Deep sleep to conserve power
        if (ENABLE_DEEP_SLEEP) {
            DEBUG_PRINTLN("Low battery - entering deep sleep");
            esp_deep_sleep(DEEP_SLEEP_DURATION_US);
        }
    }
    
    // Small delay to prevent tight loop
    delay(100);
}

// ============================================================================
// WIFI SETUP
// ============================================================================

// Try to connect to WiFi with given credentials
bool tryWiFiConnect(const char* ssid, const char* password, int timeoutMs) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    
    DEBUG_PRINTF("Connecting to %s...\n", ssid);
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
        delay(500);
        DEBUG_PRINT(".");
    }
    DEBUG_PRINTLN();
    
    return WiFi.status() == WL_CONNECTED;
}

// Start WiFi configuration portal
void startConfigPortal() {
    DEBUG_PRINTLN("Starting WiFi configuration portal...");
    display.showLoading("WiFi Setup Mode\nConnect to: BusTimetable\nThen visit: 192.168.4.1");
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP("BusTimetable", "");  // Open network
    delay(100);
    
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    
    // DNS server to redirect all domains to our IP (captive portal)
    dnsServer.start(53, "*", apIP);
    
    // Serve configuration page
    configServer.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head>";
        html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
        html += "<title>Bus Timetable WiFi Setup</title>";
        html += "<style>";
        html += "body{font-family:system-ui;background:#1a1a1a;color:#fff;margin:0;padding:20px;text-align:center;}";
        html += "h1{color:#FFB81C;}";
        html += ".card{background:#2a2a2a;border-radius:15px;padding:20px;max-width:350px;margin:20px auto;}";
        html += "input{width:100%;padding:12px;margin:8px 0;border:none;border-radius:8px;font-size:16px;box-sizing:border-box;}";
        html += ".btn{background:#FFB81C;color:#1a1a1a;border:none;padding:15px;border-radius:8px;font-size:16px;cursor:pointer;width:100%;}";
        html += "</style></head><body>";
        html += "<h1>🚌 Bus Timetable</h1>";
        html += "<p>WiFi Configuration</p>";
        html += "<div class='card'>";
        html += "<form action='/save' method='POST'>";
        html += "<input type='text' name='ssid' placeholder='WiFi Network Name' required>";
        html += "<input type='password' name='pass' placeholder='WiFi Password'>";
        html += "<input type='submit' value='Connect' class='btn'>";
        html += "</form></div>";
        html += "</body></html>";
        configServer.send(200, "text/html", html);
    });
    
    configServer.on("/save", HTTP_POST, []() {
        String ssid = configServer.arg("ssid");
        String pass = configServer.arg("pass");
        
        if (ssid.length() > 0) {
            // Save credentials
            wifiPrefs.begin("wifi", false);
            wifiPrefs.putString("ssid", ssid);
            wifiPrefs.putString("pass", pass);
            wifiPrefs.end();
            
            String html = "<!DOCTYPE html><html><head>";
            html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
            html += "<style>body{font-family:system-ui;background:#1a1a1a;color:#fff;text-align:center;padding:50px;}</style>";
            html += "</head><body>";
            html += "<h1>✓ Saved!</h1>";
            html += "<p>Rebooting to connect to: " + ssid + "</p>";
            html += "</body></html>";
            configServer.send(200, "text/html", html);
            
            delay(2000);
            ESP.restart();
        } else {
            configServer.send(400, "text/plain", "SSID required");
        }
    });
    
    // Captive portal detection endpoints
    configServer.on("/generate_204", HTTP_GET, []() { configServer.sendHeader("Location", "/"); configServer.send(302); });
    configServer.on("/fwlink", HTTP_GET, []() { configServer.sendHeader("Location", "/"); configServer.send(302); });
    configServer.onNotFound([]() { configServer.sendHeader("Location", "/"); configServer.send(302); });
    
    configServer.begin();
    configPortalActive = true;
    
    DEBUG_PRINTLN("Config portal started at 192.168.4.1");
    DEBUG_PRINTLN("Connect to WiFi: BusTimetable (no password)");
}

void setupWiFi() {
    // First, try saved credentials from Preferences
    wifiPrefs.begin("wifi", true);  // Read-only
    String savedSSID = wifiPrefs.getString("ssid", "");
    String savedPass = wifiPrefs.getString("pass", "");
    wifiPrefs.end();
    
    if (savedSSID.length() > 0) {
        DEBUG_PRINTLN("Trying saved WiFi credentials...");
        if (tryWiFiConnect(savedSSID.c_str(), savedPass.c_str(), WIFI_CONNECT_TIMEOUT_MS)) {
            wifiConnected = true;
            DEBUG_PRINTLN("WiFi connected using saved credentials!");
            DEBUG_PRINTF("IP Address: %s\n", WiFi.localIP().toString().c_str());
            DEBUG_PRINTF("Signal strength: %d dBm\n", WiFi.RSSI());
            return;
        }
        DEBUG_PRINTLN("Saved credentials failed.");
    }
    
    // Try hardcoded credentials
    DEBUG_PRINTLN("Trying default WiFi credentials...");
    if (tryWiFiConnect(WIFI_SSID, WIFI_PASSWORD, WIFI_CONNECT_TIMEOUT_MS)) {
        wifiConnected = true;
        DEBUG_PRINTLN("WiFi connected!");
        DEBUG_PRINTF("IP Address: %s\n", WiFi.localIP().toString().c_str());
        DEBUG_PRINTF("Signal strength: %d dBm\n", WiFi.RSSI());
        return;
    }
    
    // All connection attempts failed - start config portal
    DEBUG_PRINTLN("All WiFi connection attempts failed.");
    wifiConnected = false;
    startConfigPortal();
}

// ============================================================================
// TIME SYNCHRONIZATION
// ============================================================================

void setupTime() {
    // Configure NTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    // Set timezone for UK (GMT/BST)
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
    
    DEBUG_PRINT("Waiting for time sync");
    
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        DEBUG_PRINT(".");
        delay(500);
        attempts++;
    }
    
    DEBUG_PRINTLN();
    
    if (getLocalTime(&timeinfo)) {
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        DEBUG_PRINTF("Time synchronized: %s\n", timeStr);
    } else {
        DEBUG_PRINTLN("Failed to sync time");
    }
}

void updateCurrentTime() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[6];
        strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
        currentTimeStr = String(buf);
    }
}

// ============================================================================
// BATTERY MONITORING
// ============================================================================

void readBattery() {
    // Take multiple samples to reduce noise on the ADC line
    const int samples = 8;
    int total = 0;
    for (int i = 0; i < samples; i++) {
        total += analogRead(BATTERY_PIN);
        delay(5);
    }
    float adcValue = total / (float)samples;
    
    // Convert to voltage (account for voltage divider)
    float voltage = (adcValue / 4095.0f) * BATTERY_ADC_REFERENCE * BATTERY_VOLTAGE_DIVIDER;
    
    // Calculate percentage
    float percent = ((voltage - BATTERY_VOLTAGE_EMPTY) / 
                    (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY)) * 100.0;
    
    batteryVoltage = voltage;
    batteryPercent = constrain((int)percent, 0, 100);
    
    DEBUG_PRINTF("Battery: %.2fV (%d%%)\n", batteryVoltage, batteryPercent);
}

// ============================================================================
// BUS DATA FETCHING
// ============================================================================

void fetchAndDisplayBuses() {
    Direction currentDir = transportApi.getDirection();
    
    bool success = transportApi.fetchDepartures(currentDir, departures, 20, departureCount);
    
    if (success && departureCount > 0) {
        showingPlaceholderData = false;
        lastDataFetch = millis();  // Track when we got fresh data
        DEBUG_PRINTF("Fetched %d departures\n", departureCount);
        
        // Log departures
        for (int i = 0; i < departureCount; i++) {
            DEBUG_PRINTF("  %s: %s at %s (leave in %d min)\n",
                        departures[i].busNumber.c_str(),
                        departures[i].stopName.c_str(),
                        departures[i].departureTime.c_str(),
                        departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes);
        }
    } else {
        String reason = transportApi.getLastError();
        if (!wifiConnected) {
            reason = "No WiFi";
        } else if (!success && reason.length() == 0) {
            reason = "API error";
        } else if (departureCount == 0 && reason.length() == 0) {
            reason = "No departures";
        }
        populatePlaceholderDepartures(reason);
        DEBUG_PRINTF("Failed to fetch departures: %s\n", 
                    transportApi.getLastError().c_str());
    }
    
    // Update display
    display.showBusTimetable(departures, departureCount,
                              currentTimeStr, transportApi.getDirectionLabel(),
                              batteryPercent, wifiConnected, showingPlaceholderData);
    unsigned long now = millis();
    lastCountdownUpdate = now;
    lastDisplayRefresh = now;
}

void populatePlaceholderDepartures(const String& reason) {
    // No placeholder data - just show empty state
    showingPlaceholderData = true;
    departureCount = 0;
    DEBUG_PRINTF("No live data available: %s\n", reason.c_str());
}

void handleDisplayTick(unsigned long now) {
    if (departureCount == 0 && !showingPlaceholderData) {
        return;
    }
    
    if (now - lastDisplayRefresh < DISPLAY_PARTIAL_REFRESH_INTERVAL) {
        return;
    }
    
    if (lastCountdownUpdate == 0) {
        lastCountdownUpdate = now;
    }
    
    if (departureCount > 0) {
        unsigned long elapsed = now - lastCountdownUpdate;
        if (elapsed >= 60000) {
            unsigned long minutesElapsed = elapsed / 60000;
            decrementDepartureCountdowns(minutesElapsed);
            lastCountdownUpdate += minutesElapsed * 60000;
        }
    }
    
    display.showBusTimetable(departures, departureCount,
                              currentTimeStr, transportApi.getDirectionLabel(),
                              batteryPercent, wifiConnected, showingPlaceholderData);
    lastDisplayRefresh = now;
}

void decrementDepartureCountdowns(unsigned long minutesElapsed) {
    if (minutesElapsed == 0) return;
    
    for (int i = 0; i < departureCount; i++) {
        int updated = departures[i].minutesUntilDeparture - (int)minutesElapsed;
        if (updated < 0) {
            updated = 0;
        }
        departures[i].minutesUntilDeparture = updated;
    }
}

String formatFutureTime(int minutesAhead) {
    if (minutesAhead < 0) {
        minutesAhead = 0;
    }
    
    time_t now = time(nullptr);
    if (now <= 0) {
        return "--:--";
    }
    
    now += minutesAhead * 60;
    struct tm futureTime;
    if (!localtime_r(&now, &futureTime)) {
        return "--:--";
    }
    
    char buf[6];
    if (strftime(buf, sizeof(buf), "%H:%M", &futureTime) == 0) {
        return "--:--";
    }
    return String(buf);
}

// ============================================================================
// MQTT STATE PUBLISHING
// ============================================================================

void publishMqttState() {
    mqtt.publishState(
        batteryPercent,
        batteryVoltage,
        WiFi.RSSI(),
        transportApi.getDirectionLabel(),
        departureCount,
        WiFi.localIP().toString(),
        FIRMWARE_VERSION
    );
}

// ============================================================================
// MQTT COMMAND HANDLER
// ============================================================================

void handleMqttCommand(const String& command) {
    DEBUG_PRINTF("Received command: %s\n", command.c_str());
    
    if (command == "refresh") {
        DEBUG_PRINTLN("Manual refresh requested");
        fetchAndDisplayBuses();
        publishMqttState();
    }
    else if (command == "toggle_direction") {
        DEBUG_PRINTLN("Direction toggle requested");
        Direction current = transportApi.getDirection();
        Direction newDir = (current == TO_CHELTENHAM) ? TO_CHURCHDOWN : TO_CHELTENHAM;
        transportApi.setDirection(newDir);
        fetchAndDisplayBuses();
        publishMqttState();
    }
    else if (command == "reboot") {
        DEBUG_PRINTLN("Reboot requested");
        mqtt.publishUnavailable();
        delay(500);
        ESP.restart();
    }
    else if (command == "check_update") {
        DEBUG_PRINTLN("Update check requested");
        if (otaManager.checkForUpdate()) {
            DEBUG_PRINTLN("Update available, performing update...");
            otaManager.performUpdate(otaManager.getUpdateUrl());
        }
    }
}
