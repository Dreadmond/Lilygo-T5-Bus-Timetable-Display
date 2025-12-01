#ifndef TRANSPORT_API_H
#define TRANSPORT_API_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include "config.h"
#include "display.h"

// ============================================================================
// TRANSPORT API CLIENT
// Fetches live bus departure data from TransportAPI.com
// ============================================================================

// Direction enum
enum Direction {
    TO_CHELTENHAM,
    TO_CHURCHDOWN
};

// Stop configuration
struct BusStop {
    const char* atcocode;
    const char* name;
    int walkingTimeMinutes;
};

class TransportAPIClient {
public:
    TransportAPIClient();
    
    // Initialize the client
    void init();
    
    // Fetch departures for current direction
    bool fetchDepartures(Direction direction, BusDeparture* departures, int maxDepartures, int& count);
    
    // Switch direction
    void setDirection(Direction dir);
    Direction getDirection() const;
    String getDirectionLabel() const;
    
    // Get last error
    String getLastError() const;
    
    // Check if we're in active hours
    bool isActiveHours() const;

private:
    Direction currentDirection;
    String lastError;
    WiFiClientSecure secureClient;
    
    // Stop configurations
    static const BusStop cheltenhamStops[];
    static const BusStop churchdownStops[];
    static const int cheltenhamStopCount;
    static const int churchdownStopCount;
    
    // Routes to filter
    static const char* targetRoutes[];
    static const int routeCount;
    
    // Destinations to filter
    static const char* cheltenhamDestinations[];
    static const char* churchdownDestinations[];
    
    // Build API URL
    String buildUrl(const char* atcocode);
    
    // Parse departure time
    void parseDepartureTime(const String& timeStr, const String& estimateStr,
                           String& displayTime, int& minutesUntil);
    
    // Parse a single stop's departures
    bool parseStopDepartures(const String& jsonResponse, const BusStop& stop,
                            BusDeparture* departures, int& currentCount, int maxCount);
    
    // Check if destination matches filter
    bool isValidDestination(const String& destination, Direction dir);
    
    // Check if route is in target routes
    bool isTargetRoute(const String& route);
};

extern TransportAPIClient transportApi;

#endif // TRANSPORT_API_H

