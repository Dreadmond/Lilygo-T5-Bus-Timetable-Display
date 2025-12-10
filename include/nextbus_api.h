#ifndef NEXTBUS_API_H
#define NEXTBUS_API_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include "config.h"
#include "display.h"

// ============================================================================
// NEXTBUS/TRAVELINE API CLIENT
// Fetches live bus departure data from Traveline Nextbus API
// ============================================================================

// Direction enum (same as Transport API)
enum Direction {
    TO_CHELTENHAM,
    TO_CHURCHDOWN
};

class NextbusAPIClient {
public:
    NextbusAPIClient();
    
    // Initialize the client
    void init();
    
    // Fetch departures for current direction
    // Returns true if successful, and fills count with number of departures found
    // The actual number of API calls made can be retrieved with getLastApiCallCount()
    // forceFetchAll: if true, always fetch from all stops (used when refetching after buses become uncatchable)
    bool fetchDepartures(Direction direction, BusDeparture* departures, int maxDepartures, int& count, bool forceFetchAll = false);
    
    // Get the number of API calls made in the last fetchDepartures call
    int getLastApiCallCount() const;
    
    // Switch direction
    void setDirection(Direction dir);
    Direction getDirection() const;
    String getDirectionLabel() const;
    
    // Get last error
    String getLastError() const;
    
    // Check if we're in active hours
    bool isActiveHours() const;
    
    // Check if we can make an API call (rate limiting)
    bool canMakeApiCall() const;
    
    // Get remaining API calls for today
    int getRemainingCallsToday() const;

private:
    Direction currentDirection;
    String lastError;
    WiFiClient httpClient;  // HTTP (not HTTPS) for Traveline API
    int lastApiCallCount;  // Track API calls made in last fetch
    int messageIdCounter;  // For SIRI-SM MessageIdentifier
    
    // Stop configurations (same as Transport API)
    struct BusStop {
        const char* atcocode;
        const char* name;
        int walkingTimeMinutes;
    };
    
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
    
    // Build SIRI-SM XML request
    String buildSiriRequest(const char* atcocode);
    
    // Get current timestamp in ISO 8601 format
    String getCurrentTimestamp();
    
    // Parse departure time from ISO 8601 format
    void parseDepartureTime(const String& timeStr, const String& estimateStr,
                           String& displayTime, int& minutesUntil);
    
    // Parse SIRI-SM XML response
    bool parseSiriResponse(const String& xmlResponse, const BusStop& stop,
                          BusDeparture* departures, int& currentCount, int maxCount);
    
    // Check if destination matches filter
    bool isValidDestination(const String& destination, Direction dir);
    
    // Check if route is in target routes
    bool isTargetRoute(const String& route);
    
    // Check if route actually stops at the given stop
    bool isValidRouteForStop(const String& route, const char* stopAtcocode);
};

extern NextbusAPIClient nextbusApi;

#endif // NEXTBUS_API_H

