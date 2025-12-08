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

// API usage tracking
Preferences apiPrefs;
unsigned long lastApiResetDay = 0;  // Day of month when counter was last reset
int apiCallsToday = 0;

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
unsigned long lastApiCountCheck = 0;  // Track API counter reset

// Battery state
int batteryPercent = 100;
float batteryVoltage = 4.2;

// Button for color inversion (GPIO0 = boot button, has external pull-up)
#define BUTTON_PIN 0
bool invertedColors = false;
unsigned long lastButtonPress = 0;
const unsigned long BUTTON_DEBOUNCE_MS = 500;

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
void resetApiCounterIfNewDay();
void loadApiCounter();
void saveApiCounter();
unsigned long calculateOptimalRefreshInterval();
void incrementApiCallCount(int calls);

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
    
    // Button disabled - causes false triggers
    // pinMode(BUTTON_PIN, INPUT);
    
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
        
        // Check for OTA updates on startup (optional - can be enabled for immediate updates)
        // Updates will also be checked hourly in the main loop
        // DEBUG_PRINTLN("Checking for firmware updates on startup...");
        // if (otaManager.checkForUpdate()) {
        //     DEBUG_PRINTLN("Update available! Installing...");
        //     display.showLoading("Installing update...");
        //     otaManager.performUpdate(otaManager.getUpdateUrl());
        // }
        
        // Load and initialize API usage counter
        loadApiCounter();
        resetApiCounterIfNewDay();
        
        // Initial battery read
        readBattery();
        
        // Fetch initial bus data
        DEBUG_PRINTLN("Fetching initial bus data...");
        display.showLoading("Loading bus times...");
        if (transportApi.isActiveHours()) {
            fetchAndDisplayBuses();
        } else {
            // Sleep mode - show nice clock display
            display.showClock(currentTimeStr);
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
                                  batteryPercent, wifiConnected, showingPlaceholderData,
                                  true);  // Full refresh entering sleep
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
    
    // Check and reset API counter at midnight
    if (now - lastApiCountCheck >= 60000) {  // Check every minute
        resetApiCounterIfNewDay();
        lastApiCountCheck = now;
    }
    
    // Calculate optimal refresh interval based on remaining API calls and time
    unsigned long refreshInterval = calculateOptimalRefreshInterval();
    
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
    
    // Button disabled - use MQTT command "invert_colors" instead
    
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
    display.showWiFiSetup("BusTimetable", "192.168.4.1");
    
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
        html += "<h1>Bus Timetable</h1>";
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
    
    // Calculate percentage - ensure voltages >= FULL show as 100%
    float percent;
    if (voltage >= BATTERY_VOLTAGE_FULL) {
        percent = 100.0;
    } else {
        percent = ((voltage - BATTERY_VOLTAGE_EMPTY) / 
                   (BATTERY_VOLTAGE_FULL - BATTERY_VOLTAGE_EMPTY)) * 100.0;
    }
    
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
    
    // Get the actual number of API calls made (optimized fetch stops early when it has enough)
    int actualApiCalls = transportApi.getLastApiCallCount();
    
    // Increment API call counter with actual calls made
    incrementApiCallCount(actualApiCalls);
    
    DEBUG_PRINTF("API calls: %d calls made (optimized from max %d stops)\n",
                 actualApiCalls, (currentDir == TO_CHELTENHAM) ? 3 : 2);
    
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
        
        // If we have fewer than 3 buses, try fetching more stops (if we didn't already fetch all)
        // This ensures we always try to show 3 buses during active hours
        if (departureCount < 3 && wifiConnected && transportApi.isActiveHours()) {
            DEBUG_PRINTF("Only got %d buses, but this may be all available. Displaying what we have.\n", departureCount);
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
    
    // Update display with full refresh (new data from API)
    display.showBusTimetable(departures, departureCount,
                              currentTimeStr, transportApi.getDirectionLabel(),
                              batteryPercent, wifiConnected, showingPlaceholderData,
                              true);  // Force full refresh for new data
    unsigned long now = millis();
    lastCountdownUpdate = now;
    lastDisplayRefresh = now;
}

void populatePlaceholderDepartures(const String& reason) {
    // Fake placeholder data for UI testing
    showingPlaceholderData = true;
    DEBUG_PRINTF("Using placeholder data: %s\n", reason.c_str());
    
    // Fake bus 1
    departures[0].busNumber = "X1";
    departures[0].stopName = "Fake Stop Alpha";
    departures[0].destination = "Testville";
    departures[0].departureTime = "99:99";
    departures[0].minutesUntilDeparture = 42;
    departures[0].walkingTimeMinutes = 5;
    departures[0].isLive = false;
    departures[0].statusText = "DEMO DATA";
    
    // Fake bus 2
    departures[1].busNumber = "Z9";
    departures[1].stopName = "Sample Road";
    departures[1].destination = "Nowhere";
    departures[1].departureTime = "88:88";
    departures[1].minutesUntilDeparture = 99;
    departures[1].walkingTimeMinutes = 10;
    departures[1].isLive = false;
    departures[1].statusText = "DEMO DATA";
    
    // Fake bus 3
    departures[2].busNumber = "00";
    departures[2].stopName = "Placeholder Lane";
    departures[2].destination = "Example City";
    departures[2].departureTime = "77:77";
    departures[2].minutesUntilDeparture = 123;
    departures[2].walkingTimeMinutes = 15;
    departures[2].isLive = false;
    departures[2].statusText = "DEMO DATA";
    
    departureCount = 3;
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
    
    // Use partial refresh for countdown updates (faster, less flashing)
    display.showBusTimetable(departures, departureCount,
                              currentTimeStr, transportApi.getDirectionLabel(),
                              batteryPercent, wifiConnected, showingPlaceholderData,
                              false);  // Partial refresh
    lastDisplayRefresh = now;
}

void decrementDepartureCountdowns(unsigned long minutesElapsed) {
    if (minutesElapsed == 0) return;
    
    bool needsRefetch = false;
    
    for (int i = 0; i < departureCount; i++) {
        int updated = departures[i].minutesUntilDeparture - (int)minutesElapsed;
        if (updated < 0) {
            updated = 0;
        }
        departures[i].minutesUntilDeparture = updated;
        
        // Check if this bus is now impossible to catch (leaveIn < 0)
        int leaveIn = departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes;
        if (leaveIn < 0) {
            // Mark for removal
            needsRefetch = true;
        }
    }
    
    // If any buses became impossible to catch, filter them out
    if (needsRefetch) {
        int filtered = 0;
        for (int i = 0; i < departureCount; i++) {
            int leaveIn = departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes;
            if (leaveIn >= 0) {
                // Keep this bus
                if (filtered != i) {
                    departures[filtered] = departures[i];
                }
                filtered++;
            } else {
                DEBUG_PRINTF("Removing bus %s - too late (leave in %d min, walk %d min)\n",
                            departures[i].busNumber.c_str(),
                            departures[i].minutesUntilDeparture,
                            departures[i].walkingTimeMinutes);
            }
        }
        
        int removed = departureCount - filtered;
        departureCount = filtered;
        
        if (removed > 0) {
            DEBUG_PRINTF("Removed %d bus(es) that can't be caught. Remaining: %d\n", removed, departureCount);
            
            // If we have fewer than 3 buses, trigger a refetch to get more data
            if (departureCount < 3 && wifiConnected && transportApi.isActiveHours()) {
                DEBUG_PRINTLN("Triggering API refetch to get more departures...");
                // Reset timer to trigger immediate refetch on next loop iteration
                lastBusUpdate = millis() - BUS_DATA_REFRESH_INTERVAL_MS;
            }
        }
    }
}

// ============================================================================
// API USAGE TRACKING AND RATE LIMITING
// ============================================================================

void loadApiCounter() {
    apiPrefs.begin("api", true);  // Read-only
    apiCallsToday = apiPrefs.getInt("calls", 0);
    lastApiResetDay = apiPrefs.getULong("lastReset", 0);
    apiPrefs.end();
    DEBUG_PRINTF("Loaded API counter: %d calls today, last reset day: %lu\n", apiCallsToday, lastApiResetDay);
}

void saveApiCounter() {
    apiPrefs.begin("api", false);
    apiPrefs.putInt("calls", apiCallsToday);
    apiPrefs.putULong("lastReset", lastApiResetDay);
    apiPrefs.end();
}

void resetApiCounterIfNewDay() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return;  // Can't check time, skip reset
    }
    
    unsigned long currentDay = timeinfo.tm_mday;
    
    if (currentDay != lastApiResetDay) {
        DEBUG_PRINTF("New day detected (day %lu). Resetting API counter from %d.\n", currentDay, apiCallsToday);
        apiCallsToday = 0;
        lastApiResetDay = currentDay;
        saveApiCounter();
    }
}

void incrementApiCallCount(int calls) {
    apiCallsToday += calls;
    saveApiCounter();
    DEBUG_PRINTF("API calls today: %d/%d\n", apiCallsToday, TRANSPORT_API_DAILY_LIMIT);
}

unsigned long calculateOptimalRefreshInterval() {
    // Reset counter if new day
    resetApiCounterIfNewDay();
    
    // Get current time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // Can't get time, use default interval
        return BUS_DATA_REFRESH_INTERVAL_MS;
    }
    
    // Calculate remaining active hours in the day
    int currentHour = timeinfo.tm_hour;
    int remainingActiveHours = 0;
    
    if (currentHour < ACTIVE_HOURS_START) {
        // Before active hours - full day remaining
        remainingActiveHours = ACTIVE_HOURS_END - ACTIVE_HOURS_START;
    } else if (currentHour >= ACTIVE_HOURS_END) {
        // After active hours - no more today
        remainingActiveHours = 0;
    } else {
        // During active hours
        remainingActiveHours = ACTIVE_HOURS_END - currentHour;
    }
    
    if (remainingActiveHours <= 0) {
        // Not in active hours or no time remaining
        return BUS_DATA_REFRESH_INTERVAL_MS;
    }
    
    // Calculate remaining API calls
    int remainingCalls = TRANSPORT_API_DAILY_LIMIT - apiCallsToday;
    
    if (remainingCalls <= 0) {
        // Out of API calls for today - use very long interval
        DEBUG_PRINTLN("WARNING: API limit reached for today! Using 1-hour interval.");
        return 3600000;  // 1 hour
    }
    
    // Estimate stops per refresh based on direction
    // With optimization: typically 1-2 stops for Cheltenham (often just 1), 1 for Churchdown
    // We use a conservative estimate to ensure even distribution
    Direction currentDir = transportApi.getDirection();
    int avgStopsPerRefresh = (currentDir == TO_CHELTENHAM) ? 1.5 : 1;  // More optimistic: 1.5 avg for Cheltenham
    
    // Calculate how many refreshes we can do with remaining calls
    // Use integer math but account for fractional average
    int maxRefreshes;
    if (currentDir == TO_CHELTENHAM) {
        // For Cheltenham: 1.5 avg = 3 calls per 2 refreshes, so 2 refreshes per 3 calls
        maxRefreshes = (remainingCalls * 2) / 3;
    } else {
        // For Churchdown: 1 call per refresh
        maxRefreshes = remainingCalls;
    }
    
    if (maxRefreshes <= 0) {
        // Can't even do one refresh
        DEBUG_PRINTLN("WARNING: Not enough API calls for even one refresh!");
        return 3600000;  // 1 hour
    }
    
    // Calculate interval: spread refreshes evenly over remaining hours
    // Convert hours to milliseconds
    unsigned long remainingMs = remainingActiveHours * 3600000UL;
    
    // Calculate optimal interval: total time / number of refreshes
    unsigned long optimalInterval = remainingMs / maxRefreshes;
    
    // Ensure minimum interval of 5 minutes (300000ms) and max of 30 minutes (1800000ms)
    // But allow up to 60 minutes if we're running low on API calls to ensure even distribution
    if (optimalInterval < 300000) {
        optimalInterval = 300000;  // Minimum 5 minutes
    } else if (optimalInterval > 3600000) {
        optimalInterval = 3600000;  // Maximum 60 minutes (if very low on calls)
    } else if (optimalInterval > 1800000 && remainingCalls > 50) {
        // If we have plenty of calls left, cap at 30 minutes for better user experience
        optimalInterval = 1800000;
    }
    
    DEBUG_PRINTF("API rate calc: %d calls used, %d remaining, %d hours left, ~%.1f avg stops/refresh -> %lu ms interval (%.1f min)\n",
                 apiCallsToday, remainingCalls, remainingActiveHours, 
                 (currentDir == TO_CHELTENHAM) ? 1.5f : 1.0f, 
                 optimalInterval, optimalInterval / 60000.0f);
    
    return optimalInterval;
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
    else if (command == "invert_colors") {
        // Force to light mode (for testing)
        DEBUG_PRINTLN("Setting LIGHT mode");
        invertedColors = true;
        display.setInvertedColors(true);
        display.showBusTimetable(departures, departureCount,
                                  currentTimeStr, transportApi.getDirectionLabel(),
                                  batteryPercent, wifiConnected, showingPlaceholderData,
                                  true);
        unsigned long now = millis();
        lastDisplayRefresh = now;
        lastBusUpdate = now;
        lastCountdownUpdate = now;
    }
    else if (command == "dark_mode") {
        // Force to dark mode
        DEBUG_PRINTLN("Setting DARK mode");
        invertedColors = false;
        display.setInvertedColors(false);
        display.showBusTimetable(departures, departureCount,
                                  currentTimeStr, transportApi.getDirectionLabel(),
                                  batteryPercent, wifiConnected, showingPlaceholderData,
                                  true);
        unsigned long now = millis();
        lastDisplayRefresh = now;
        lastBusUpdate = now;
        lastCountdownUpdate = now;
    }
}
