#ifndef CONFIG_H
#define CONFIG_H

#include "../src/secrets.h"  // API keys and secrets

// ============================================================================
// BUS TIMETABLE E-INK DISPLAY CONFIGURATION
// LilyGo T5 4.7" (960x540 pixels)
// ============================================================================

// ----------------------------------------------------------------------------
// VERSION INFO
// ----------------------------------------------------------------------------
#define FIRMWARE_VERSION "1.0.0"
#define DEVICE_NAME "bus-timetable-eink"
#define DEVICE_FRIENDLY_NAME "Bus Timetable Display"

// ----------------------------------------------------------------------------
// WIFI CONFIGURATION
// ----------------------------------------------------------------------------
#define WIFI_SSID SECRET_WIFI_SSID
#define WIFI_PASSWORD SECRET_WIFI_PASSWORD
#define WIFI_CONNECT_TIMEOUT_MS 30000
#define WIFI_RETRY_DELAY_MS 500

// ----------------------------------------------------------------------------
// MQTT CONFIGURATION (Home Assistant Auto-Discovery)
// ----------------------------------------------------------------------------
#define MQTT_SERVER SECRET_MQTT_SERVER
#define MQTT_PORT SECRET_MQTT_PORT
#define MQTT_USER SECRET_MQTT_USER
#define MQTT_PASSWORD SECRET_MQTT_PASSWORD
#define MQTT_CLIENT_ID SECRET_MQTT_CLIENT_ID

// Home Assistant Discovery prefix
#define HA_DISCOVERY_PREFIX "homeassistant"

// MQTT Topics
#define MQTT_STATE_TOPIC "bus_timetable/state"
#define MQTT_AVAILABILITY_TOPIC "bus_timetable/availability"
#define MQTT_COMMAND_TOPIC "bus_timetable/command"

// ----------------------------------------------------------------------------
// TRANSPORT API CONFIGURATION
// ----------------------------------------------------------------------------
#define TRANSPORT_API_ID SECRET_TRANSPORT_API_ID
#define TRANSPORT_API_KEY SECRET_TRANSPORT_API_KEY
#define TRANSPORT_API_BASE "https://transportapi.com"

// Bus stops configuration (from your React app)
// Cheltenham direction stops
#define STOP_HARE_HOUNDS "1600GL1187"
#define STOP_ST_JOHNS "1600GLA577"
#define STOP_LIBRARY "1600GLA569"

// Churchdown direction stops  
#define STOP_PROM_3 "1600GLA36692"
#define STOP_PROM_5 "1600GL1076"

// Walking time overrides (minutes)
#define WALK_TIME_HARE_HOUNDS 12
#define WALK_TIME_ST_JOHNS 5
#define WALK_TIME_LIBRARY 4
#define WALK_TIME_CHELTENHAM 2

// Routes to filter
#define BUS_ROUTES {"94", "95", "97", "98"}
#define NUM_ROUTES 4

// ----------------------------------------------------------------------------
// DISPLAY CONFIGURATION
// ----------------------------------------------------------------------------
#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 540

// Refresh intervals
#define DISPLAY_FULL_REFRESH_INTERVAL 3600000  // Full refresh every hour (ms)
#define DISPLAY_PARTIAL_REFRESH_INTERVAL 60000 // Partial refresh every minute

// ----------------------------------------------------------------------------
// DATA REFRESH CONFIGURATION
// ----------------------------------------------------------------------------
#define BUS_DATA_REFRESH_INTERVAL_MS 600000    // Refresh bus data every 10 minutes (within active hours)
#define BUS_DATA_REFRESH_SLOW_MS 600000        // Slow refresh every 10 minutes (used for placeholder updates)
#define ACTIVE_HOURS_START 6                    // 6 AM
#define ACTIVE_HOURS_END 22                     // 10 PM
#define TRANSPORT_API_DAILY_LIMIT 300           // Max API calls allowed per day

// ----------------------------------------------------------------------------
// BATTERY MONITORING
// ----------------------------------------------------------------------------
#define BATTERY_PIN 14                          // ADC pin for battery voltage (ESP32-S3)
#define BATTERY_VOLTAGE_FULL 4.2               // Full battery voltage
#define BATTERY_VOLTAGE_EMPTY 3.3              // Empty battery voltage
#define BATTERY_READ_INTERVAL_MS 60000         // Read battery every minute

// ADC calibration (adjust based on your voltage divider)
#define BATTERY_ADC_REFERENCE 3.3
#define BATTERY_VOLTAGE_DIVIDER 2.0            // If using 100k/100k divider

// ----------------------------------------------------------------------------
// OTA UPDATE CONFIGURATION
// ----------------------------------------------------------------------------
#define OTA_GITHUB_USER "matt"  // UPDATE THIS to your actual GitHub username
#define OTA_GITHUB_REPO "bus-timetable-eink"
#define OTA_CHECK_INTERVAL_MS 3600000          // Check for updates every hour

// ----------------------------------------------------------------------------
// WEATHER CONFIGURATION (OpenWeatherMap)
// ----------------------------------------------------------------------------
#define WEATHER_API_KEY SECRET_OWM_APIKEY
#define WEATHER_LAT_STR SECRET_LAT
#define WEATHER_LON_STR SECRET_LON
#define WEATHER_REFRESH_MS 600000                      // Refresh weather every 10 minutes

// ----------------------------------------------------------------------------
// DEEP SLEEP CONFIGURATION
// ----------------------------------------------------------------------------
#define ENABLE_DEEP_SLEEP false                 // Set to true for battery operation
#define DEEP_SLEEP_DURATION_US 60000000        // 60 seconds in microseconds

// ----------------------------------------------------------------------------
// DEBUG CONFIGURATION
// ----------------------------------------------------------------------------
#define DEBUG_SERIAL true
#define DEBUG_BAUD_RATE 115200

#if DEBUG_SERIAL
    #define DEBUG_PRINT(x) Serial.print(x)
    #define DEBUG_PRINTLN(x) Serial.println(x)
    #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
    #define DEBUG_PRINT(x)
    #define DEBUG_PRINTLN(x)
    #define DEBUG_PRINTF(...)
#endif

#endif // CONFIG_H
