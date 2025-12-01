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
#include <time.h>
#include "config.h"
#include "display.h"
#include "transport_api.h"
#include "mqtt_ha.h"
#include "ota_update.h"
#include "weather.h"

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
unsigned long lastWeatherUpdate = 0;

// Battery state
int batteryPercent = 100;
float batteryVoltage = 4.2;

// Bus data
BusDeparture departures[5];
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
        
        // Initial battery read
        readBattery();
        
        // Fetch initial weather
        DEBUG_PRINTLN("Fetching initial weather...");
        weatherClient.fetchWeather();
        lastWeatherUpdate = millis();
        
        // Fetch initial bus data
        DEBUG_PRINTLN("Fetching initial bus data...");
        display.showLoading("Loading bus times...");
        if (transportApi.isActiveHours()) {
            fetchAndDisplayBuses();
        } else {
            populatePlaceholderDepartures("Sleeping hours 22:00-06:00");
            WeatherData w = weatherClient.getWeather();
            display.showBusTimetable(departures, departureCount,
                                     currentTimeStr, transportApi.getDirectionLabel(),
                                     batteryPercent, wifiConnected, showingPlaceholderData,
                                     w.valid ? w.temperature : 0,
                                     w.valid ? w.condition : "");
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
    unsigned long now = millis();
    
    // Update current time string
    updateCurrentTime();
    bool activeHours = transportApi.isActiveHours();
    
    if (!activeHours && !sleepModeActive) {
        populatePlaceholderDepartures("Sleeping hours 22:00-06:00");
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
    
    // Fetch weather
    if (wifiConnected && (now - lastWeatherUpdate >= WEATHER_REFRESH_MS)) {
        DEBUG_PRINTLN("Fetching weather...");
        weatherClient.fetchWeather();
        lastWeatherUpdate = now;
        // Refresh display to show new weather
        WeatherData w = weatherClient.getWeather();
        display.showBusTimetable(departures, departureCount,
                                  currentTimeStr, transportApi.getDirectionLabel(),
                                  batteryPercent, wifiConnected, showingPlaceholderData,
                                  w.valid ? w.temperature : 0,
                                  w.valid ? w.condition : "");
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

void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    DEBUG_PRINTF("Connecting to %s", WIFI_SSID);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
           (millis() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(WIFI_RETRY_DELAY_MS);
        DEBUG_PRINT(".");
    }
    
    DEBUG_PRINTLN();
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        DEBUG_PRINTLN("WiFi connected!");
        DEBUG_PRINTF("IP Address: %s\n", WiFi.localIP().toString().c_str());
        DEBUG_PRINTF("Signal strength: %d dBm\n", WiFi.RSSI());
    } else {
        wifiConnected = false;
        DEBUG_PRINTLN("WiFi connection failed!");
    }
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
    
    bool success = transportApi.fetchDepartures(currentDir, departures, 5, departureCount);
    
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
    
    // Get weather data
    WeatherData weather = weatherClient.getWeather();
    float temp = weather.valid ? weather.temperature : 0;
    String condition = weather.valid ? weather.condition : "";
    
    // Update display
    display.showBusTimetable(departures, departureCount,
                              currentTimeStr, transportApi.getDirectionLabel(),
                              batteryPercent, wifiConnected, showingPlaceholderData,
                              temp, condition);
    unsigned long now = millis();
    lastCountdownUpdate = now;
    lastDisplayRefresh = now;
}

void populatePlaceholderDepartures(const String& reason) {
    showingPlaceholderData = true;
    
    struct PlaceholderEntry {
        const char* busNumber;
        const char* stopName;
        const char* destination;
        int minutesAhead;
        int walkingMinutes;
    };
    
    static const PlaceholderEntry cheltenhamSamples[] = {
        {"94", "St John's Church", "Cheltenham Spa", 8, WALK_TIME_ST_JOHNS},
        {"94", "Churchdown Library", "Cheltenham Spa", 22, WALK_TIME_LIBRARY},
        {"97", "Hare & Hounds", "Cheltenham Spa", 37, WALK_TIME_HARE_HOUNDS}
    };
    
    static const PlaceholderEntry churchdownSamples[] = {
        {"94", "Promenade (Stop 3)", "Gloucester Transport Hub", 6, WALK_TIME_CHELTENHAM},
        {"94", "Promenade (Stop 5)", "Churchdown", 18, WALK_TIME_CHELTENHAM},
        {"97", "Promenade (Stop 3)", "Brockworth", 32, WALK_TIME_CHELTENHAM}
    };
    
    Direction dir = transportApi.getDirection();
    const PlaceholderEntry* set = (dir == TO_CHELTENHAM) ? cheltenhamSamples : churchdownSamples;
    int setCount = (dir == TO_CHELTENHAM) ? (int)(sizeof(cheltenhamSamples) / sizeof(cheltenhamSamples[0]))
                                          : (int)(sizeof(churchdownSamples) / sizeof(churchdownSamples[0]));
    
    int maxSlots = (int)(sizeof(departures) / sizeof(departures[0]));
    departureCount = min(setCount, maxSlots);
    
    for (int i = 0; i < departureCount; i++) {
        const PlaceholderEntry& tpl = set[i];
        departures[i].busNumber = tpl.busNumber;
        departures[i].stopName = tpl.stopName;
        departures[i].destination = tpl.destination;
        departures[i].minutesUntilDeparture = tpl.minutesAhead;
        departures[i].walkingTimeMinutes = tpl.walkingMinutes;
        departures[i].departureTime = formatFutureTime(tpl.minutesAhead);
        departures[i].isLive = false;
        
        if (i == 0) {
            departures[i].statusText = "Live data unavailable";
            if (reason.length() > 0) {
                departures[i].statusText += " (" + reason + ")";
            }
        } else {
            departures[i].statusText = "Sample timetable";
        }
    }
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
    
    WeatherData w = weatherClient.getWeather();
    display.showBusTimetable(departures, departureCount,
                              currentTimeStr, transportApi.getDirectionLabel(),
                              batteryPercent, wifiConnected, showingPlaceholderData,
                              w.valid ? w.temperature : 0,
                              w.valid ? w.condition : "");
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
