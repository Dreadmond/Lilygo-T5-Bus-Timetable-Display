#include "transport_api.h"

// ============================================================================
// TRANSPORT API CLIENT IMPLEMENTATION
// ============================================================================

TransportAPIClient transportApi;

// Stop configurations
const BusStop TransportAPIClient::cheltenhamStops[] = {
    {STOP_HARE_HOUNDS, "Hare & Hounds", WALK_TIME_HARE_HOUNDS},
    {STOP_ST_JOHNS, "St John's Church", WALK_TIME_ST_JOHNS},
    {STOP_LIBRARY, "Churchdown Library", WALK_TIME_LIBRARY}
};

const BusStop TransportAPIClient::churchdownStops[] = {
    {STOP_PROM_3, "Promenade (Stop 3)", WALK_TIME_CHELTENHAM},
    {STOP_PROM_5, "Promenade (Stop 5)", WALK_TIME_CHELTENHAM}
};

const int TransportAPIClient::cheltenhamStopCount = 3;
const int TransportAPIClient::churchdownStopCount = 2;

const char* TransportAPIClient::targetRoutes[] = {"94", "95", "97", "98"};
const int TransportAPIClient::routeCount = 4;

const char* TransportAPIClient::cheltenhamDestinations[] = {
    "cheltenham", "cheltenham spa", "chelt", "promenade"
};

const char* TransportAPIClient::churchdownDestinations[] = {
    "gloucester", "gloucester transport hub", "transport hub", "churchdown"
};

TransportAPIClient::TransportAPIClient() {
    currentDirection = TO_CHELTENHAM;
}

void TransportAPIClient::init() {
    // Configure secure client for HTTPS
    secureClient.setInsecure(); // Skip certificate verification for simplicity
    DEBUG_PRINTLN("Transport API client initialized");
}

void TransportAPIClient::setDirection(Direction dir) {
    currentDirection = dir;
    DEBUG_PRINTF("Direction changed to: %s\n", getDirectionLabel().c_str());
}

Direction TransportAPIClient::getDirection() const {
    return currentDirection;
}

String TransportAPIClient::getDirectionLabel() const {
    return currentDirection == TO_CHELTENHAM ? "Cheltenham Spa" : "Churchdown";
}

String TransportAPIClient::getLastError() const {
    return lastError;
}

bool TransportAPIClient::isActiveHours() const {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return true; // Default to active if time unknown
    }
    int hour = timeinfo.tm_hour;
    return (hour >= ACTIVE_HOURS_START && hour < ACTIVE_HOURS_END);
}

String TransportAPIClient::buildUrl(const char* atcocode) {
    String url = String(TRANSPORT_API_BASE) + "/v3/uk/bus/stop/" + atcocode + "/live.json";
    url += "?app_id=" + String(TRANSPORT_API_ID);
    url += "&app_key=" + String(TRANSPORT_API_KEY);
    url += "&group=route&nextbuses=yes&limit=8";
    return url;
}

bool TransportAPIClient::fetchDepartures(Direction direction, BusDeparture* departures, 
                                          int maxDepartures, int& count) {
    count = 0;
    lastError = "";
    
    const BusStop* stops;
    int stopCount;
    
    if (direction == TO_CHELTENHAM) {
        stops = cheltenhamStops;
        stopCount = cheltenhamStopCount;
    } else {
        stops = churchdownStops;
        stopCount = churchdownStopCount;
    }
    
    DEBUG_PRINTF("Fetching departures for %d stops\n", stopCount);
    
    HTTPClient http;
    
    for (int i = 0; i < stopCount && count < maxDepartures; i++) {
        String url = buildUrl(stops[i].atcocode);
        DEBUG_PRINTF("Fetching: %s\n", stops[i].name);
        
        http.begin(secureClient, url);
        http.setTimeout(15000);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        
        int httpCode = http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            String response = http.getString();
            
            if (!parseStopDepartures(response, stops[i], departures, count, maxDepartures)) {
                DEBUG_PRINTF("Warning: Failed to parse departures for %s\n", stops[i].name);
            }
        } else {
            DEBUG_PRINTF("HTTP error for %s: %d\n", stops[i].name, httpCode);
            lastError = "HTTP " + String(httpCode);
        }
        
        http.end();
        
        // Small delay between requests to be nice to the API
        delay(100);
    }
    
    // Sort departures by time
    if (count > 1) {
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (departures[j].minutesUntilDeparture < departures[i].minutesUntilDeparture) {
                    BusDeparture temp = departures[i];
                    departures[i] = departures[j];
                    departures[j] = temp;
                }
            }
        }
    }
    
    // Filter to only show buses we can catch (after walking)
    int filtered = 0;
    for (int i = 0; i < count; i++) {
        int leaveIn = departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes;
        if (leaveIn >= 0) {
            if (filtered != i) {
                departures[filtered] = departures[i];
            }
            filtered++;
        }
    }
    count = min(filtered, 3); // Max 3 buses on display
    
    DEBUG_PRINTF("Found %d valid departures\n", count);
    return count > 0;
}

bool TransportAPIClient::parseStopDepartures(const String& jsonResponse, const BusStop& stop,
                                              BusDeparture* departures, int& currentCount, 
                                              int maxCount) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);
    
    if (error) {
        DEBUG_PRINTF("JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    JsonObject departuresObj = doc["departures"];
    if (departuresObj.isNull()) {
        return false;
    }
    
    // Iterate through routes
    for (int r = 0; r < routeCount && currentCount < maxCount; r++) {
        const char* route = targetRoutes[r];
        JsonArray routeDepartures = departuresObj[route];
        
        if (!routeDepartures.isNull()) {
            for (JsonObject dep : routeDepartures) {
                if (currentCount >= maxCount) break;
                
                String direction = dep["direction"].as<String>();
                String line = dep["line"].as<String>();
                
                // Check if direction matches our filter
                if (!isValidDestination(direction, currentDirection)) {
                    continue;
                }
                
                // Extract departure info
                String expectedTime = dep["expected_departure_time"].as<String>();
                String aimedTime = dep["aimed_departure_time"].as<String>();
                String bestEstimate = dep["best_departure_estimate"].as<String>();
                
                // Calculate minutes until departure
                String displayTime;
                int minutesUntil;
                parseDepartureTime(expectedTime.length() > 0 ? expectedTime : aimedTime, 
                                  bestEstimate, displayTime, minutesUntil);
                
                // Skip if bus already departed
                if (minutesUntil < 0) continue;
                
                // Determine if live or scheduled
                bool isLive = expectedTime.length() > 0;
                
                // Calculate delay
                String statusText = "";
                if (isLive && aimedTime.length() > 0) {
                    String unused;
                    int aimedMinutes;
                    parseDepartureTime(aimedTime, "", unused, aimedMinutes);
                    int delay = minutesUntil - aimedMinutes;
                    if (delay >= 2) {
                        statusText = "Delayed " + String(delay) + " min";
                    } else if (delay <= -2) {
                        statusText = "Early " + String(-delay) + " min";
                    } else {
                        statusText = "On time";
                    }
                } else {
                    statusText = isLive ? "Live" : "Scheduled";
                }
                
                // Build departure entry
                departures[currentCount].busNumber = line;
                departures[currentCount].stopName = String(stop.name);
                departures[currentCount].destination = direction;
                departures[currentCount].departureTime = displayTime;
                departures[currentCount].minutesUntilDeparture = minutesUntil;
                departures[currentCount].walkingTimeMinutes = stop.walkingTimeMinutes;
                departures[currentCount].isLive = isLive;
                departures[currentCount].statusText = statusText;
                
                currentCount++;
            }
        }
    }
    
    return true;
}

void TransportAPIClient::parseDepartureTime(const String& timeStr, const String& estimateStr,
                                             String& displayTime, int& minutesUntil) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        displayTime = timeStr;
        minutesUntil = 0;
        return;
    }
    
    // Try to parse estimate minutes first
    if (estimateStr.length() > 0) {
        int estimate = estimateStr.toInt();
        if (estimate > 0 || estimateStr == "0") {
            minutesUntil = estimate;
            
            // Calculate display time from estimate
            int depHour = timeinfo.tm_hour;
            int depMin = timeinfo.tm_min + estimate;
            while (depMin >= 60) {
                depMin -= 60;
                depHour++;
            }
            depHour %= 24;
            
            char buf[6];
            snprintf(buf, sizeof(buf), "%02d:%02d", depHour, depMin);
            displayTime = String(buf);
            return;
        }
    }
    
    // Parse time string (HH:MM format)
    if (timeStr.length() >= 5) {
        int depHour = timeStr.substring(0, 2).toInt();
        int depMin = timeStr.substring(3, 5).toInt();
        
        displayTime = timeStr.substring(0, 5);
        
        // Calculate minutes until
        int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        int depMinutes = depHour * 60 + depMin;
        
        // Handle overnight
        if (depMinutes < nowMinutes - 60) {
            depMinutes += 24 * 60;
        }
        
        minutesUntil = depMinutes - nowMinutes;
    } else {
        displayTime = "--:--";
        minutesUntil = 0;
    }
}

bool TransportAPIClient::isValidDestination(const String& destination, Direction dir) {
    String lower = destination;
    lower.toLowerCase();
    
    const char** targets;
    int numTargets;
    
    if (dir == TO_CHELTENHAM) {
        targets = cheltenhamDestinations;
        numTargets = 4;
    } else {
        targets = churchdownDestinations;
        numTargets = 4;
    }
    
    for (int i = 0; i < numTargets; i++) {
        if (lower.indexOf(targets[i]) >= 0) {
            return true;
        }
    }
    
    return false;
}

bool TransportAPIClient::isTargetRoute(const String& route) {
    for (int i = 0; i < routeCount; i++) {
        if (route == targetRoutes[i]) {
            return true;
        }
    }
    return false;
}

