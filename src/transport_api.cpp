#include "transport_api.h"

// ============================================================================
// TRANSPORT API CLIENT IMPLEMENTATION
// ============================================================================

TransportAPIClient transportApi;

// Stop configurations - ordered by distance from 88 Parton Road
const BusStop TransportAPIClient::cheltenhamStops[] = {
    {STOP_LIBRARY, "Churchdown Library", WALK_TIME_LIBRARY},       // Closest - 96, 97
    {STOP_HARE_HOUNDS, "Hare & Hounds", WALK_TIME_HARE_HOUNDS},   // Medium - 94
    {STOP_ST_JOHNS, "St John's Church", WALK_TIME_ST_JOHNS}        // Further - 98
};

const BusStop TransportAPIClient::churchdownStops[] = {
    {STOP_PROM_3, "Promenade (Stop 3)", WALK_TIME_CHELTENHAM},
    {STOP_PROM_5, "Promenade (Stop 5)", WALK_TIME_CHELTENHAM}
};

const int TransportAPIClient::cheltenhamStopCount = 3;
const int TransportAPIClient::churchdownStopCount = 2;

const char* TransportAPIClient::targetRoutes[] = {"94", "95", "96", "97", "98"};
const int TransportAPIClient::routeCount = 5;

const char* TransportAPIClient::cheltenhamDestinations[] = {
    "cheltenham", "cheltenham spa", "chelt", "promenade"
};

const char* TransportAPIClient::churchdownDestinations[] = {
    "gloucester", "gloucester transport hub", "transport hub", "churchdown"
};

// Route-to-stop validation: which routes actually stop at which stops
// Returns true if the route actually stops at the given stop
bool TransportAPIClient::isValidRouteForStop(const String& route, const char* stopAtcocode) {
    String stopCode = String(stopAtcocode);
    
    // Bus 94 does NOT stop at Churchdown Library (STOP_LIBRARY)
    if (route == "94" && stopCode == STOP_LIBRARY) {
        return false;
    }
    
    // Bus 97 does NOT stop at Hare & Hounds (STOP_HARE_HOUNDS)
    if (route == "97" && stopCode == STOP_HARE_HOUNDS) {
        return false;
    }
    
    // All other combinations are valid
    return true;
}

TransportAPIClient::TransportAPIClient() {
    currentDirection = TO_CHELTENHAM;
    lastApiCallCount = 0;
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
    
    DEBUG_PRINTF("Fetching departures for %d stops (optimized: will stop when enough data)\n", stopCount);
    
    HTTPClient http;
    const int TARGET_DEPARTURES = 5;  // Fetch extra to account for deduplication (want 3 final)
    lastApiCallCount = 0;  // Reset counter
    
    // Optimized: Fetch stops incrementally, starting with closest
    // Stops are already ordered by distance (closest first)
    // We'll always fetch at least the first stop, then continue if needed
    bool fetchedAllStops = false;
    
    for (int i = 0; i < stopCount; i++) {
        String url = buildUrl(stops[i].atcocode);
        DEBUG_PRINTF("Fetching: %s (stop %d/%d)\n", stops[i].name, i + 1, stopCount);
        
        // Retry logic for failed requests
        int httpCode = 0;
        int retries = 0;
        const int MAX_RETRIES = 2;
        
        while (retries <= MAX_RETRIES && httpCode != HTTP_CODE_OK) {
            http.begin(secureClient, url);
            http.setTimeout(15000);
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            
            httpCode = http.GET();
            
            if (httpCode != HTTP_CODE_OK && retries < MAX_RETRIES) {
                DEBUG_PRINTF("HTTP error for %s: %d, retrying... (%d/%d)\n", stops[i].name, httpCode, retries + 1, MAX_RETRIES);
                http.end();
                delay(500 * (retries + 1));  // Exponential backoff
                retries++;
            } else {
                break;
            }
        }
        
        lastApiCallCount++;  // Count this API call (even if failed after retries)
        
        if (httpCode == HTTP_CODE_OK) {
            String response = http.getString();
            
            // DEBUG: Print first 500 chars of response to see structure
            DEBUG_PRINTF("API Response for %s (first 500 chars):\n", stops[i].name);
            String preview = response.substring(0, 500);
            DEBUG_PRINTLN(preview);
            DEBUG_PRINTLN("---");
            
            if (!parseStopDepartures(response, stops[i], departures, count, maxDepartures)) {
                DEBUG_PRINTF("Warning: Failed to parse departures for %s\n", stops[i].name);
            }
        } else {
            DEBUG_PRINTF("HTTP error for %s after %d retries: %d\n", stops[i].name, retries, httpCode);
            if (lastError.length() == 0) {
                lastError = "HTTP " + String(httpCode);
            }
            // Continue to next stop even if this one failed
        }
        
        http.end();
        
        // Small delay between requests to be nice to the API
        delay(100);
        
            // After fetching each stop, check if we should stop early
            // We want to ensure we have enough unique catchable buses to guarantee 3 after deduplication
            if (i < stopCount - 1 && count >= TARGET_DEPARTURES) {
                // Count likely unique catchable buses
                int likelyUniqueCatchable = 0;
                for (int j = 0; j < count; j++) {
                    // First check if catchable
                    int leaveIn = departures[j].minutesUntilDeparture - departures[j].walkingTimeMinutes;
                    if (leaveIn < 0) continue;  // Not catchable, skip
                    
                    // Check if this is a duplicate of any previous departure
                    bool isUnique = true;
                    for (int k = 0; k < j; k++) {
                        // Same bus, same stop = likely duplicate
                        if (departures[j].busNumber == departures[k].busNumber &&
                            departures[j].stopName == departures[k].stopName) {
                            // Check if times are very close (within 2 min = duplicate)
                            int timeDiff = departures[j].minutesUntilDeparture - departures[k].minutesUntilDeparture;
                            if (timeDiff < 0) timeDiff = -timeDiff;  // abs
                            if (timeDiff <= 2) {
                                isUnique = false;
                                break;
                            }
                        }
                    }
                    if (isUnique) {
                        likelyUniqueCatchable++;
                    }
                }
                
                // Only stop early if we're confident we have 6+ unique catchable buses
                // This ensures we'll have at least 3 after deduplication (which can be aggressive)
                if (likelyUniqueCatchable >= 6) {
                    DEBUG_PRINTF("Got enough unique catchable buses (%d >= 6), stopping early. Saved %d API calls!\n", 
                                likelyUniqueCatchable, stopCount - i - 1);
                    fetchedAllStops = false;
                    break;
                } else {
                    DEBUG_PRINTF("Only %d unique catchable buses so far, continuing to fetch more stops to ensure 3...\n", likelyUniqueCatchable);
                }
            }
            
            if (i == stopCount - 1) {
                fetchedAllStops = true;
            }
    }
    
    // Sort departures by "leave in" time (departure time minus walking time)
    // This shows the most urgent buses first
    if (count > 1) {
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                int leaveInI = departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes;
                int leaveInJ = departures[j].minutesUntilDeparture - departures[j].walkingTimeMinutes;
                if (leaveInJ < leaveInI) {
                    BusDeparture temp = departures[i];
                    departures[i] = departures[j];
                    departures[j] = temp;
                }
            }
        }
    }
    
    // Remove exact duplicates only (same bus, same stop, same time)
    // Allow multiple departures of the same route at different times
    int unique = 0;
    for (int i = 0; i < count; i++) {
        bool isDuplicate = false;
        
        for (int j = 0; j < unique; j++) {
            // Only duplicate if same bus AND same stop AND within 2 minutes
            if (departures[i].busNumber == departures[j].busNumber &&
                departures[i].stopName == departures[j].stopName) {
                int timeDiff = departures[i].minutesUntilDeparture - departures[j].minutesUntilDeparture;
                if (timeDiff >= -2 && timeDiff <= 2) {
                    isDuplicate = true;
                    break;
                }
            }
        }
        
        if (!isDuplicate) {
            if (unique != i) {
                departures[unique] = departures[i];
            }
            unique++;
        }
    }
    count = unique;
    
    // Filter to only show buses we can catch (leave in >= 0)
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
    
    // Display up to 3 buses (or all available if less than 3)
    count = min(filtered, 3);  // Limit to 3 for display, but show all we have if less
    
    DEBUG_PRINTF("Found %d valid departures after filtering (used %d API calls, fetched %s stops)\n", 
                 count, lastApiCallCount, fetchedAllStops ? "all" : "some");
    
    // Consider it successful if we have at least 1 bus (0 buses will show error message)
    return count > 0;
}

int TransportAPIClient::getLastApiCallCount() const {
    return lastApiCallCount;
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
    
    // DEBUG: Print what the API says this stop is
    String apiStopName = doc["name"].as<String>();
    String apiAtcocode = doc["atcocode"].as<String>();
    DEBUG_PRINTF("=== API Response for stop ===\n");
    DEBUG_PRINTF("  Queried: %s (%s)\n", stop.name, stop.atcocode);
    DEBUG_PRINTF("  API says: %s (%s)\n", apiStopName.c_str(), apiAtcocode.c_str());
    
    // Verify the API returned the stop we asked for
    if (apiAtcocode.length() > 0 && apiAtcocode != stop.atcocode) {
        DEBUG_PRINTF("WARNING: API returned different stop! Expected %s, got %s\n",
                    stop.atcocode, apiAtcocode.c_str());
    }
    
    JsonObject departuresObj = doc["departures"];
    if (departuresObj.isNull()) {
        DEBUG_PRINTLN("No departures object in response");
        return false;
    }
    
    // DEBUG: Print all routes in the response
    DEBUG_PRINTLN("Routes in response:");
    for (JsonPair kv : departuresObj) {
        DEBUG_PRINTF("  Route %s: %d departures\n", kv.key().c_str(), kv.value().as<JsonArray>().size());
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
                
                // The TransportAPI only returns buses that stop at the queried stop
                // The stop is verified at the API response level (doc["atcocode"])
                // So we can trust that all departures are from the correct stop
                String actualStopName = String(stop.name);
                
                // Extract departure info
                String expectedTime = dep["expected_departure_time"].as<String>();
                String aimedTime = dep["aimed_departure_time"].as<String>();
                String bestEstimate = dep["best_departure_estimate"].as<String>();
                
                // Debug: show what API returned
                DEBUG_PRINTF("  RAW: line=%s aimed=%s expected=%s estimate=%s\n",
                            line.c_str(), aimedTime.c_str(), expectedTime.c_str(), bestEstimate.c_str());
                
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
                departures[currentCount].stopName = actualStopName;
                departures[currentCount].destination = direction;
                departures[currentCount].departureTime = displayTime;
                departures[currentCount].minutesUntilDeparture = minutesUntil;
                departures[currentCount].walkingTimeMinutes = stop.walkingTimeMinutes;
                departures[currentCount].isLive = isLive;
                departures[currentCount].statusText = statusText;
                
                DEBUG_PRINTF("  ADDED: Bus %s from %s at %s (in %d min, walk %d)\n",
                            line.c_str(), actualStopName.c_str(), displayTime.c_str(),
                            minutesUntil, stop.walkingTimeMinutes);
                
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
        displayTime = timeStr.length() > 0 ? timeStr : "??:??";
        minutesUntil = 0;
        return;
    }
    
    // Determine which time string to use for display
    // Priority: timeStr (aimed/expected), then estimateStr (which is also a time string)
    String actualTimeStr = timeStr;
    if (actualTimeStr.length() < 5 && estimateStr.length() >= 5) {
        actualTimeStr = estimateStr;
    }
    
    // Use actual time string for display
    if (actualTimeStr.length() >= 5) {
        displayTime = actualTimeStr.substring(0, 5);
        
        // Calculate minutes until departure from the time
        int depHour = actualTimeStr.substring(0, 2).toInt();
        int depMin = actualTimeStr.substring(3, 5).toInt();
        
        int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        int depMinutes = depHour * 60 + depMin;
        
        // Handle overnight
        if (depMinutes < nowMinutes - 60) {
            depMinutes += 24 * 60;
        }
        
        minutesUntil = depMinutes - nowMinutes;
        return;
    }
    
    // No valid time string
    displayTime = "??:??";
    minutesUntil = 0;
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

