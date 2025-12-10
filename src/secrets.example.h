/**
 * secrets.example.h
 *
 * Copy this file to src/secrets.h and fill in your real credentials.
 * The actual secrets file is ignored by git so sensitive data never
 * ends up in source control.
 */

#ifndef SECRETS_EXAMPLE_H
#define SECRETS_EXAMPLE_H

// ---------------------------------------------------------------------------
// WiFi credentials
// ---------------------------------------------------------------------------
#define SECRET_WIFI_SSID        "YOUR_WIFI_SSID"
#define SECRET_WIFI_PASSWORD    "YOUR_WIFI_PASSWORD"

// ---------------------------------------------------------------------------
// MQTT broker (Home Assistant / telemetry)
// ---------------------------------------------------------------------------
#define SECRET_MQTT_SERVER      "mqtt.example.com"
#define SECRET_MQTT_PORT        1883
#define SECRET_MQTT_USER        "mqtt_user"
#define SECRET_MQTT_PASSWORD    "mqtt_password"
#define SECRET_MQTT_CLIENT_ID   "bus_timetable_eink"

// ---------------------------------------------------------------------------
// TransportAPI credentials (ORIGINAL - kept for regression)
// https://www.transportapi.com/
// ---------------------------------------------------------------------------
#define SECRET_TRANSPORT_API_ID   "your_transportapi_app_id"
#define SECRET_TRANSPORT_API_KEY  "your_transportapi_api_key"

// ---------------------------------------------------------------------------
// Nextbus/Traveline API credentials (NEW - PRIMARY API)
// DO NOT COMMIT THESE TO GITHUB!
// ---------------------------------------------------------------------------
#define SECRET_NEXTBUS_USERNAME   "your_nextbus_username"
#define SECRET_NEXTBUS_PASSWORD   "your_nextbus_password"

// ---------------------------------------------------------------------------
// OpenWeatherMap
// ---------------------------------------------------------------------------
#define SECRET_OWM_APIKEY       "your_openweather_api_key"

// ---------------------------------------------------------------------------
// Location for weather + journey estimates
// ---------------------------------------------------------------------------
#define SECRET_LAT              "51.500"
#define SECRET_LON              "-0.124"
#define SECRET_CITY_STRING      "London"

// ---------------------------------------------------------------------------
// Optional Home Assistant integration
// ---------------------------------------------------------------------------
#define SECRET_HA_HOST            "homeassistant.local"
#define SECRET_HA_PORT            8123
#define SECRET_HA_TOKEN           "ha_long_lived_access_token"
#define SECRET_HA_TEMP_ENTITY     "sensor.some_temperature"
#define SECRET_HA_HUMIDITY_ENTITY "sensor.some_humidity"

// ---------------------------------------------------------------------------
// Optional NextCloud integration (photo/cartoon modes)
// ---------------------------------------------------------------------------
#define SECRET_NEXTCLOUD_URL      "https://nextcloud.example.com/remote.php/dav/files/user/epaper/"
#define SECRET_NEXTCLOUD_USER     "username"
#define SECRET_NEXTCLOUD_PASS     "password"
#define SECRET_NEXTCLOUD_PHOTO    "photo.png"
#define SECRET_NEXTCLOUD_CARTOON  "cartoon.png"

#endif // SECRETS_EXAMPLE_H
