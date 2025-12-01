#include "mqtt_ha.h"
#include <WiFi.h>

// ============================================================================
// MQTT HOME ASSISTANT IMPLEMENTATION
// Auto-discovery for seamless integration
// ============================================================================

MQTTHomeAssistant mqtt;
MQTTHomeAssistant* MQTTHomeAssistant::instance = nullptr;

MQTTHomeAssistant::MQTTHomeAssistant() : mqttClient(wifiClient) {
    discoveryPublished = false;
    lastReconnectAttempt = 0;
    commandCallback = nullptr;
    instance = this;
}

void MQTTHomeAssistant::init() {
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(1024); // Larger buffer for discovery messages
    
    DEBUG_PRINTLN("MQTT client initialized");
    DEBUG_PRINTF("Server: %s:%d\n", MQTT_SERVER, MQTT_PORT);
}

bool MQTTHomeAssistant::connect() {
    if (mqttClient.connected()) {
        return true;
    }
    
    unsigned long now = millis();
    if (now - lastReconnectAttempt < 5000) {
        return false; // Don't retry too frequently
    }
    lastReconnectAttempt = now;
    
    DEBUG_PRINTLN("Connecting to MQTT...");
    
    String clientId = String(MQTT_CLIENT_ID) + "_" + getDeviceId();
    
    // Set last will for availability
    bool connected = mqttClient.connect(
        clientId.c_str(),
        MQTT_USER,
        MQTT_PASSWORD,
        MQTT_AVAILABILITY_TOPIC,
        0,      // QoS
        true,   // Retain
        "offline"
    );
    
    if (connected) {
        DEBUG_PRINTLN("MQTT connected!");
        
        // Publish availability
        publishAvailable();
        
        // Subscribe to command topic
        mqttClient.subscribe(MQTT_COMMAND_TOPIC);
        
        // Publish discovery config if not already done
        if (!discoveryPublished) {
            publishDiscoveryConfig();
            discoveryPublished = true;
        }
        
        return true;
    }
    
    DEBUG_PRINTF("MQTT connection failed, rc=%d\n", mqttClient.state());
    return false;
}

void MQTTHomeAssistant::loop() {
    if (!mqttClient.connected()) {
        connect();
    }
    mqttClient.loop();
}

bool MQTTHomeAssistant::isConnected() {
    return mqttClient.connected();
}

void MQTTHomeAssistant::mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (instance == nullptr) return;
    
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    DEBUG_PRINTF("MQTT message on %s: %s\n", topic, message.c_str());
    
    if (String(topic) == MQTT_COMMAND_TOPIC && instance->commandCallback) {
        instance->commandCallback(message);
    }
}

void MQTTHomeAssistant::setCommandCallback(void (*callback)(const String& command)) {
    commandCallback = callback;
}

String MQTTHomeAssistant::getDeviceId() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[13];
    snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(id);
}

String MQTTHomeAssistant::getMacAddress() {
    return WiFi.macAddress();
}

String MQTTHomeAssistant::getDeviceInfo() {
    JsonDocument deviceDoc;
    
    deviceDoc["identifiers"][0] = "bus_timetable_" + getDeviceId();
    deviceDoc["name"] = DEVICE_FRIENDLY_NAME;
    deviceDoc["model"] = "LilyGo T5 4.7\" E-Paper";
    deviceDoc["manufacturer"] = "LilyGo";
    deviceDoc["sw_version"] = FIRMWARE_VERSION;
    deviceDoc["configuration_url"] = "http://" + WiFi.localIP().toString();
    
    String deviceInfo;
    serializeJson(deviceDoc, deviceInfo);
    return deviceInfo;
}

void MQTTHomeAssistant::publishDiscoveryConfig() {
    DEBUG_PRINTLN("Publishing Home Assistant discovery config...");
    
    String deviceId = getDeviceId();
    
    // Battery percentage sensor
    publishSensorDiscovery(
        "Battery",
        "battery",
        "battery",
        "%",
        "{{ value_json.battery_percent }}",
        "mdi:battery"
    );
    
    // Battery voltage sensor
    publishSensorDiscovery(
        "Battery Voltage",
        "battery_voltage",
        "voltage",
        "V",
        "{{ value_json.battery_voltage }}",
        "mdi:flash"
    );
    
    // WiFi signal strength
    publishSensorDiscovery(
        "WiFi Signal",
        "wifi_rssi",
        "signal_strength",
        "dBm",
        "{{ value_json.rssi }}",
        "mdi:wifi"
    );
    
    // Current direction
    publishSensorDiscovery(
        "Direction",
        "direction",
        nullptr,
        nullptr,
        "{{ value_json.direction }}",
        "mdi:bus"
    );
    
    // Bus count
    publishSensorDiscovery(
        "Buses Displayed",
        "bus_count",
        nullptr,
        "buses",
        "{{ value_json.bus_count }}",
        "mdi:bus-clock"
    );
    
    // IP Address
    publishSensorDiscovery(
        "IP Address",
        "ip_address",
        nullptr,
        nullptr,
        "{{ value_json.ip_address }}",
        "mdi:ip-network"
    );
    
    // Firmware version
    publishSensorDiscovery(
        "Firmware",
        "firmware",
        nullptr,
        nullptr,
        "{{ value_json.version }}",
        "mdi:chip"
    );
    
    // Refresh button
    publishButtonDiscovery(
        "Refresh Display",
        "refresh",
        "refresh",
        "mdi:refresh"
    );
    
    // Toggle direction button
    publishButtonDiscovery(
        "Toggle Direction",
        "toggle_direction",
        "toggle_direction",
        "mdi:swap-horizontal"
    );
    
    DEBUG_PRINTLN("Discovery config published");
}

void MQTTHomeAssistant::publishSensorDiscovery(const char* name, const char* uniqueId,
                                                const char* deviceClass, const char* unit,
                                                const char* valueTemplate, const char* icon) {
    JsonDocument doc;
    String deviceId = getDeviceId();
    
    doc["name"] = name;
    doc["unique_id"] = String("bus_timetable_") + deviceId + "_" + uniqueId;
    doc["state_topic"] = MQTT_STATE_TOPIC;
    doc["availability_topic"] = MQTT_AVAILABILITY_TOPIC;
    doc["value_template"] = valueTemplate;
    
    if (deviceClass) {
        doc["device_class"] = deviceClass;
    }
    if (unit) {
        doc["unit_of_measurement"] = unit;
    }
    if (icon) {
        doc["icon"] = icon;
    }
    
    // Device info
    doc["device"]["identifiers"][0] = "bus_timetable_" + deviceId;
    doc["device"]["name"] = DEVICE_FRIENDLY_NAME;
    doc["device"]["model"] = "LilyGo T5 4.7\" E-Paper";
    doc["device"]["manufacturer"] = "LilyGo";
    doc["device"]["sw_version"] = FIRMWARE_VERSION;
    
    String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/bus_timetable_" + 
                   deviceId + "/" + uniqueId + "/config";
    
    String payload;
    serializeJson(doc, payload);
    
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void MQTTHomeAssistant::publishButtonDiscovery(const char* name, const char* uniqueId,
                                                const char* command, const char* icon) {
    JsonDocument doc;
    String deviceId = getDeviceId();
    
    doc["name"] = name;
    doc["unique_id"] = String("bus_timetable_") + deviceId + "_" + uniqueId;
    doc["command_topic"] = MQTT_COMMAND_TOPIC;
    doc["payload_press"] = command;
    doc["availability_topic"] = MQTT_AVAILABILITY_TOPIC;
    
    if (icon) {
        doc["icon"] = icon;
    }
    
    // Device info
    doc["device"]["identifiers"][0] = "bus_timetable_" + deviceId;
    doc["device"]["name"] = DEVICE_FRIENDLY_NAME;
    doc["device"]["model"] = "LilyGo T5 4.7\" E-Paper";
    doc["device"]["manufacturer"] = "LilyGo";
    doc["device"]["sw_version"] = FIRMWARE_VERSION;
    
    String topic = String(HA_DISCOVERY_PREFIX) + "/button/bus_timetable_" + 
                   deviceId + "/" + uniqueId + "/config";
    
    String payload;
    serializeJson(doc, payload);
    
    mqttClient.publish(topic.c_str(), payload.c_str(), true);
}

void MQTTHomeAssistant::publishState(int batteryPercent, float batteryVoltage,
                                      int rssi, const String& direction,
                                      int busCount, const String& ipAddress,
                                      const String& version) {
    if (!mqttClient.connected()) return;
    
    JsonDocument doc;
    
    doc["battery_percent"] = batteryPercent;
    doc["battery_voltage"] = batteryVoltage;
    doc["rssi"] = rssi;
    doc["direction"] = direction;
    doc["bus_count"] = busCount;
    doc["ip_address"] = ipAddress;
    doc["version"] = version;
    
    String payload;
    serializeJson(doc, payload);
    
    mqttClient.publish(MQTT_STATE_TOPIC, payload.c_str(), true);
    DEBUG_PRINTLN("Published state to MQTT");
}

void MQTTHomeAssistant::publishAvailable() {
    mqttClient.publish(MQTT_AVAILABILITY_TOPIC, "online", true);
}

void MQTTHomeAssistant::publishUnavailable() {
    mqttClient.publish(MQTT_AVAILABILITY_TOPIC, "offline", true);
}

