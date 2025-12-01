#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ============================================================================
// MQTT CLIENT WITH HOME ASSISTANT AUTO-DISCOVERY
// Publishes device state, battery info, and other telemetry
// ============================================================================

class MQTTHomeAssistant {
public:
    MQTTHomeAssistant();
    
    // Initialize and connect
    void init();
    bool connect();
    void loop();
    bool isConnected();
    
    // Publish Home Assistant discovery configs
    void publishDiscoveryConfig();
    
    // Publish current state
    void publishState(int batteryPercent, float batteryVoltage, 
                      int rssi, const String& direction,
                      int busCount, const String& ipAddress,
                      const String& version);
    
    // Publish availability
    void publishAvailable();
    void publishUnavailable();
    
    // Handle incoming commands
    void setCommandCallback(void (*callback)(const String& command));

private:
    WiFiClient wifiClient;
    PubSubClient mqttClient;
    bool discoveryPublished;
    unsigned long lastReconnectAttempt;
    void (*commandCallback)(const String& command);
    
    // Unique device ID based on MAC
    String getDeviceId();
    String getMacAddress();
    
    // Discovery message builders
    void publishSensorDiscovery(const char* name, const char* uniqueId,
                                const char* deviceClass, const char* unit,
                                const char* valueTemplate, const char* icon);
    void publishBinarySensorDiscovery(const char* name, const char* uniqueId,
                                      const char* deviceClass, const char* valueTemplate);
    void publishButtonDiscovery(const char* name, const char* uniqueId,
                                const char* command, const char* icon);
    void publishSelectDiscovery();
    
    // Get device info JSON for discovery
    String getDeviceInfo();
    
    // MQTT callback
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
    static MQTTHomeAssistant* instance;
};

extern MQTTHomeAssistant mqtt;

#endif // MQTT_HA_H

