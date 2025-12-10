#include "nextbus_api.h"

// ============================================================================
// NEXTBUS/TRAVELINE API CLIENT IMPLEMENTATION
// ============================================================================

NextbusAPIClient nextbusApi;

// Stop configurations - ordered by distance from 88 Parton Road
// (Same as Transport API - using same stop codes)
const NextbusAPIClient::BusStop NextbusAPIClient::cheltenhamStops[] = {
    {STOP_LIBRARY, "Churchdown Library", WALK_TIME_LIBRARY},       // Closest - 96, 97
    {STOP_HARE_HOUNDS, "Hare & Hounds", WALK_TIME_HARE_HOUNDS},   // Medium - 94
    {STOP_ST_JOHNS, "St John's Church", WALK_TIME_ST_JOHNS}        // Further - 98
};

const NextbusAPIClient::BusStop NextbusAPIClient::churchdownStops[] = {
    {STOP_PROM_3, "Promenade (Stop 3)", WALK_TIME_CHELTENHAM},
    {STOP_PROM_5, "Promenade (Stop 5)", WALK_TIME_CHELTENHAM}
};

const int NextbusAPIClient::cheltenhamStopCount = 3;
const int NextbusAPIClient::churchdownStopCount = 2;

const char* NextbusAPIClient::targetRoutes[] = {"94", "95", "96", "97", "98"};
const int NextbusAPIClient::routeCount = 5;

const char* NextbusAPIClient::cheltenhamDestinations[] = {
    "cheltenham", "cheltenham spa", "chelt", "promenade"
};

const char* NextbusAPIClient::churchdownDestinations[] = {
    "gloucester", "gloucester transport hub", "transport hub", "churchdown"
};

// Route-to-stop validation: which routes actually stop at which stops
bool NextbusAPIClient::isValidRouteForStop(const String& route, const char* stopAtcocode) {
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

NextbusAPIClient::NextbusAPIClient() {
    currentDirection = TO_CHELTENHAM;
    lastApiCallCount = 0;
    messageIdCounter = 1;
}

void NextbusAPIClient::init() {
    // HTTP client (not HTTPS) for Traveline API
    DEBUG_PRINTLN("Nextbus API client initialized (SIRI-SM XML format)");
}

void NextbusAPIClient::setDirection(Direction dir) {
    currentDirection = dir;
    DEBUG_PRINTF("Direction changed to: %s\n", getDirectionLabel().c_str());
}

Direction NextbusAPIClient::getDirection() const {
    return currentDirection;
}

String NextbusAPIClient::getDirectionLabel() const {
    return currentDirection == TO_CHELTENHAM ? "Cheltenham Spa" : "Churchdown";
}

String NextbusAPIClient::getLastError() const {
    return lastError;
}

bool NextbusAPIClient::isActiveHours() const {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return true; // Default to active if time unknown
    }
    int hour = timeinfo.tm_hour;
    return (hour >= ACTIVE_HOURS_START && hour < ACTIVE_HOURS_END);
}

bool NextbusAPIClient::canMakeApiCall() const {
    // This will be checked in main.cpp using the global API counter
    // We just return true here - rate limiting is handled at a higher level
    return true;
}

int NextbusAPIClient::getRemainingCallsToday() const {
    // This will be calculated in main.cpp
    // For now, return a safe default
    return NEXTBUS_API_DAILY_LIMIT;
}


String NextbusAPIClient::getCurrentTimestamp() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return "1970-01-01T00:00:00Z";
    }
    
    char timestamp[25];
    // Format: 2014-07-01T15:09:12Z (ISO 8601 UTC)
    // Note: ESP32 time is local, but API expects UTC - adjust if needed
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min,
             timeinfo.tm_sec);
    return String(timestamp);
}

String NextbusAPIClient::buildSiriRequest(const char* atcocode) {
    // Build SIRI-SM XML request according to Traveline API documentation v2.7
    // Reference: https://mytraveline.info/documentation/Traveline%20API%20Guidance%20for%20Developers%202.7.pdf
    
    String timestamp = getCurrentTimestamp();
    String messageId = String(messageIdCounter++);
    
    String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml += "<Siri version=\"1.0\" xmlns=\"http://www.siri.org.uk/\">\n";
    xml += "    <ServiceRequest>\n";
    xml += "        <RequestTimestamp>" + timestamp + "</RequestTimestamp>\n";
    xml += "        <RequestorRef>" + String(NEXTBUS_API_USERNAME) + "</RequestorRef>\n";
    xml += "        <StopMonitoringRequest version=\"1.0\">\n";
    xml += "            <RequestTimestamp>" + timestamp + "</RequestTimestamp>\n";
    xml += "            <MessageIdentifier>" + messageId + "</MessageIdentifier>\n";
    xml += "            <MonitoringRef>" + String(atcocode) + "</MonitoringRef>\n";
    xml += "        </StopMonitoringRequest>\n";
    xml += "    </ServiceRequest>\n";
    xml += "</Siri>";
    
    return xml;
}

bool NextbusAPIClient::fetchDepartures(Direction direction, BusDeparture* departures, 
                                      int maxDepartures, int& count, bool forceFetchAll) {
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
    
    if (forceFetchAll) {
        DEBUG_PRINTF("Fetching departures for ALL %d stops (force fetch - refetching after buses became uncatchable)\n", stopCount);
    } else {
        DEBUG_PRINTF("Fetching departures for %d stops (optimized: will stop when enough data)\n", stopCount);
    }
    
    HTTPClient http;
    const int TARGET_DEPARTURES = 15;  // Fetch more to ensure we get at least 3 after filtering/deduplication
    const int MIN_CATCHABLE_FOR_EARLY_STOP = 5;  // Need at least 5 catchable to stop early
    lastApiCallCount = 0;  // Reset counter
    
    // Optimized: Fetch stops incrementally, starting with closest
    bool fetchedAllStops = false;
    
    for (int i = 0; i < stopCount; i++) {
        String requestXml = buildSiriRequest(stops[i].atcocode);
        DEBUG_PRINTF("Fetching: %s (stop %d/%d)\n", stops[i].name, i + 1, stopCount);
        
        // Retry logic for failed requests
        int httpCode = 0;
        int retries = 0;
        const int MAX_RETRIES = 2;
        
        while (retries <= MAX_RETRIES && httpCode != HTTP_CODE_OK) {
            http.begin(httpClient, NEXTBUS_API_BASE);
            http.setTimeout(15000);
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            
            // Add HTTP Basic Authentication
            http.setAuthorization(NEXTBUS_API_USERNAME, NEXTBUS_API_PASSWORD);
            http.addHeader("Content-Type", "application/xml");
            
            // POST request with SIRI-SM XML body
            httpCode = http.POST(requestXml);
            
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
            
            if (!parseSiriResponse(response, stops[i], departures, count, maxDepartures)) {
                DEBUG_PRINTF("Warning: Failed to parse departures for %s (may be no buses running)\n", stops[i].name);
            }
            
            if (count == 0 && i == 0) {
                DEBUG_PRINTLN("WARNING: First stop returned no departures. This may indicate:");
                DEBUG_PRINTLN("  - No buses running on target routes (94-98)");
                DEBUG_PRINTLN("  - Wrong direction filter");
                DEBUG_PRINTLN("  - API response format issue");
            }
        } else {
            DEBUG_PRINTF("HTTP error for %s after %d retries: %d\n", stops[i].name, retries, httpCode);
            if (httpCode == HTTP_CODE_UNAUTHORIZED) {
                lastError = "Authentication failed - check credentials";
            } else if (httpCode == HTTP_CODE_FORBIDDEN) {
                lastError = "Access forbidden - check API permissions";
            } else {
                lastError = "HTTP " + String(httpCode);
            }
            // Continue to next stop even if this one failed
        }
        
        http.end();
        
        // Small delay between requests to be nice to the API
        delay(100);
        
        // After fetching each stop, check if we should stop early
        // Only stop if we have at least 5 catchable buses (to ensure we can show 3 after filtering)
        if (!forceFetchAll && i < stopCount - 1) {
            // Count catchable buses (don't worry about duplicates - we want multiple from same stop)
            int catchableCount = 0;
            for (int j = 0; j < count; j++) {
                int leaveIn = departures[j].minutesUntilDeparture - departures[j].walkingTimeMinutes;
                if (leaveIn >= 0) {
                    catchableCount++;
                }
            }
            
            // Need at least 5 catchable buses to ensure we have 3 after deduplication and filtering
            // Also need at least TARGET_DEPARTURES total to have enough data
            // IMPORTANT: Always fetch at least 2 stops to ensure we have enough variety
            // Also ensure we have at least 3 catchable buses before stopping early
            if (catchableCount >= MIN_CATCHABLE_FOR_EARLY_STOP && catchableCount >= 3 && 
                count >= TARGET_DEPARTURES && i >= 1) {
                DEBUG_PRINTF("Got enough catchable buses (%d >= %d) from %d stops, stopping early. Saved %d API calls!\n", 
                            catchableCount, MIN_CATCHABLE_FOR_EARLY_STOP, i + 1, stopCount - i - 1);
                fetchedAllStops = false;
                break;
            } else {
                DEBUG_PRINTF("Only %d catchable buses (need %d) or %d total (need %d) from %d stops, continuing...\n", 
                            catchableCount, MIN_CATCHABLE_FOR_EARLY_STOP, count, TARGET_DEPARTURES, i + 1);
            }
        } else if (forceFetchAll) {
            DEBUG_PRINTF("Force fetch mode: continuing to fetch all stops to get buses further ahead in time\n");
        }
        
        if (i == stopCount - 1) {
            fetchedAllStops = true;
        }
    }
    
    // Sort departures by "leave in" time (departure time minus walking time)
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
    
    // Remove exact duplicates only (same route, same stop, same time within 1 minute)
    // Allow multiple buses from same stop if they're at different times (e.g., 94, 94, 97)
    int unique = 0;
    for (int i = 0; i < count; i++) {
        bool isDuplicate = false;
        
        for (int j = 0; j < unique; j++) {
            // Only consider duplicate if same route, same stop, AND very close time (within 1 minute)
            if (departures[i].busNumber == departures[j].busNumber &&
                departures[i].stopName == departures[j].stopName) {
                int timeDiff = departures[i].minutesUntilDeparture - departures[j].minutesUntilDeparture;
                if (timeDiff < 0) timeDiff = -timeDiff;  // abs
                if (timeDiff <= 1) {  // Only remove if within 1 minute (exact duplicate)
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
        } else {
            DEBUG_PRINTF("Filtering out bus %s from %s: leave in %d min (departs in %d, walk %d min) - TOO LATE\n",
                        departures[i].busNumber.c_str(),
                        departures[i].stopName.c_str(),
                        leaveIn,
                        departures[i].minutesUntilDeparture,
                        departures[i].walkingTimeMinutes);
        }
    }
    
    // Return catchable buses - always show up to 3, but allow more if needed to ensure 3 unique
    // Sort by "leave in" time again after filtering
    if (filtered > 1) {
        for (int i = 0; i < filtered - 1; i++) {
            for (int j = i + 1; j < filtered; j++) {
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
    
    // Always try to show 3 buses - return all catchable buses sorted by "leave in" time
    // The display will show the first 3, but we keep all so we can always show 3 if some become uncatchable
    count = filtered;
    
    // If we have fewer than 3 catchable buses and didn't fetch all stops, return false
    // This will trigger a refetch with forceFetchAll=true
    if (count < 3 && !fetchedAllStops) {
        DEBUG_PRINTF("WARNING: Only %d catchable buses found (need 3) but didn't fetch all stops!\n", count);
        DEBUG_PRINTLN("  Will need to refetch with forceFetchAll to get more buses");
        // Don't return false here - let the caller decide if they want to refetch
        // But log the issue
    } else if (count < 3) {
        DEBUG_PRINTF("WARNING: Only %d catchable buses found (need 3) after fetching all stops. This may be due to:\n", count);
        DEBUG_PRINTLN("  - All buses already departed or too late to catch");
        DEBUG_PRINTLN("  - No buses running on target routes at this time");
        DEBUG_PRINTLN("  - Direction filtering removed all buses");
    } else {
        DEBUG_PRINTF("Successfully collected %d catchable buses - will display first 3\n", count);
    }
    
    DEBUG_PRINTF("Found %d valid departures after filtering (used %d API calls, fetched %s stops)\n", 
                 count, lastApiCallCount, fetchedAllStops ? "all" : "some");
    
    if (count == 0 && unique > 0) {
        DEBUG_PRINTLN("WARNING: All buses filtered out as uncatchable. Details:");
        for (int i = 0; i < min(unique, 5); i++) {
            int leaveIn = departures[i].minutesUntilDeparture - departures[i].walkingTimeMinutes;
            DEBUG_PRINTF("  Bus %s from %s: departs in %d min, walk %d min, leave in %d min (TOO LATE)\n",
                        departures[i].busNumber.c_str(),
                        departures[i].stopName.c_str(),
                        departures[i].minutesUntilDeparture,
                        departures[i].walkingTimeMinutes,
                        leaveIn);
        }
    }
    
    return count > 0;
}

int NextbusAPIClient::getLastApiCallCount() const {
    return lastApiCallCount;
}

// Helper functions for XML parsing
static String extractXmlTag(const String& xml, const String& tagName, int startPos = 0) {
    String openTag = "<" + tagName + ">";
    String closeTag = "</" + tagName + ">";
    
    int start = xml.indexOf(openTag, startPos);
    if (start == -1) return "";
    
    start += openTag.length();
    int end = xml.indexOf(closeTag, start);
    if (end == -1) return "";
    
    return xml.substring(start, end);
}

// Helper function to find next occurrence of a tag
static int findNextTag(const String& xml, const String& tagName, int startPos) {
    String openTag = "<" + tagName + ">";
    return xml.indexOf(openTag, startPos);
}

bool NextbusAPIClient::parseSiriResponse(const String& xmlResponse, const BusStop& stop,
                                         BusDeparture* departures, int& currentCount, 
                                         int maxCount) {
    // Parse SIRI-SM XML response according to Traveline API documentation v2.7
    // Structure: Siri > ServiceDelivery > StopMonitoringDelivery > MonitoredStopVisit[]
    
    // Check for ServiceDelivery
    if (xmlResponse.indexOf("<ServiceDelivery>") == -1) {
        DEBUG_PRINTLN("No ServiceDelivery found in response");
        return false;
    }
    
    // Find StopMonitoringDelivery
    int deliveryStart = xmlResponse.indexOf("<StopMonitoringDelivery");
    if (deliveryStart == -1) {
        DEBUG_PRINTLN("No StopMonitoringDelivery found in response");
        return true; // Not an error, just no buses
    }
    
    // Find all MonitoredStopVisit elements
    int visitPos = 0;
    int visitCount = 0;
    
    while ((visitPos = findNextTag(xmlResponse, "MonitoredStopVisit", visitPos)) != -1) {
        // Don't limit here - collect all buses, we'll filter and sort later
        // maxCount is just a safety limit to prevent buffer overflow
        if (currentCount >= maxCount) {
            DEBUG_PRINTF("Reached maxCount (%d), stopping collection from this stop\n", maxCount);
            break;
        }
        
        // Find the end of this MonitoredStopVisit
        int visitEnd = xmlResponse.indexOf("</MonitoredStopVisit>", visitPos);
        if (visitEnd == -1) break;
        
        String visitXml = xmlResponse.substring(visitPos, visitEnd);
        
        // Extract PublishedLineName (route number)
        String route = extractXmlTag(visitXml, "PublishedLineName");
        route.trim();
        
        // Check if route matches our target routes
        if (route.length() == 0 || !isTargetRoute(route)) {
            visitPos = visitEnd;
            continue;
        }
        
        // Extract DirectionName (destination)
        String direction = extractXmlTag(visitXml, "DirectionName");
        direction.trim();
        
        // Check if direction matches our filter
        if (!isValidDestination(direction, currentDirection)) {
            DEBUG_PRINTF("  SKIPPED: Bus %s - direction '%s' does not match filter\n", 
                        route.c_str(), direction.c_str());
            visitPos = visitEnd;
            continue;
        }
        
        // Extract MonitoredCall section
        int callStart = visitXml.indexOf("<MonitoredCall>");
        if (callStart == -1) {
            visitPos = visitEnd;
            continue;
        }
        int callEnd = visitXml.indexOf("</MonitoredCall>", callStart);
        if (callEnd == -1) {
            visitPos = visitEnd;
            continue;
        }
        String callXml = visitXml.substring(callStart, callEnd);
        
        // Extract AimedDepartureTime (scheduled)
        String aimedTime = extractXmlTag(callXml, "AimedDepartureTime");
        aimedTime.trim();
        
        // Extract ExpectedDepartureTime (real-time, optional)
        String expectedTime = extractXmlTag(callXml, "ExpectedDepartureTime");
        expectedTime.trim();
        
        // Use expected time if available, otherwise use aimed time
        String timeToUse = expectedTime.length() > 0 ? expectedTime : aimedTime;
        
        // Calculate minutes until departure
        String displayTime;
        int minutesUntil;
        parseDepartureTime(timeToUse, "", displayTime, minutesUntil);
        
        // Skip if bus already departed
        if (minutesUntil < 0) {
            DEBUG_PRINTF("  SKIPPED: Bus %s - already departed (minutesUntil: %d)\n", 
                        route.c_str(), minutesUntil);
            visitPos = visitEnd;
            continue;
        }
        
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
        departures[currentCount].busNumber = route;
        departures[currentCount].stopName = String(stop.name);
        departures[currentCount].destination = direction;
        departures[currentCount].departureTime = displayTime;
        departures[currentCount].minutesUntilDeparture = minutesUntil;
        departures[currentCount].walkingTimeMinutes = stop.walkingTimeMinutes;
        departures[currentCount].isLive = isLive;
        departures[currentCount].statusText = statusText;
        
        DEBUG_PRINTF("  ADDED: Bus %s from %s at %s (in %d min, walk %d)\n",
                    route.c_str(), stop.name, displayTime.c_str(),
                    minutesUntil, stop.walkingTimeMinutes);
        
        currentCount++;
        visitCount++;
        visitPos = visitEnd;
    }
    
    DEBUG_PRINTF("Parsed %d departures from SIRI-SM response\n", visitCount);
    return true;
}

void NextbusAPIClient::parseDepartureTime(const String& timeStr, const String& estimateStr,
                                         String& displayTime, int& minutesUntil) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        // Time not synced - return invalid values that will cause retry
        displayTime = "??:??";
        minutesUntil = -1;  // Negative so it gets filtered out and we retry when time syncs
        return;
    }
    
    // Determine which time string to use for display
    String actualTimeStr = timeStr;
    if (actualTimeStr.length() == 0 && estimateStr.length() > 0) {
        actualTimeStr = estimateStr;
    }
    
    // Parse ISO 8601 format: 2014-07-01T15:09:00.000+01:00 or 2014-07-01T15:09:00Z
    if (actualTimeStr.length() >= 16 && actualTimeStr.indexOf('T') > 0) {
        // Extract time portion (HH:MM) from ISO 8601
        int tPos = actualTimeStr.indexOf('T');
        if (tPos > 0) {
            String timePortion = actualTimeStr.substring(tPos + 1);
            // Remove timezone offset if present (Z, +01:00, -05:00, etc.)
            int zPos = timePortion.indexOf('Z');
            int plusPos = timePortion.indexOf('+');
            int minusPos = timePortion.indexOf('-', 1);  // Skip the minus in the time itself
            int offsetPos = -1;
            if (zPos > 0) {
                offsetPos = zPos;
            } else if (plusPos > 0) {
                offsetPos = plusPos;
            } else if (minusPos > 0) {
                offsetPos = minusPos;
            }
            if (offsetPos > 0) {
                timePortion = timePortion.substring(0, offsetPos);
            }
            
            int colonPos = timePortion.indexOf(':');
            if (colonPos > 0) {
                int depHour = timePortion.substring(0, colonPos).toInt();
                int depMin = timePortion.substring(colonPos + 1, colonPos + 3).toInt();
                
                // Format display time
                char timeBuf[6];
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", depHour, depMin);
                displayTime = String(timeBuf);
                
                // Calculate minutes until departure
                // Note: API times are in local UK time (GMT/BST), so no conversion needed
                int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
                int depMinutes = depHour * 60 + depMin;
                
                // Handle overnight (if departure is more than 12 hours in the past, assume next day)
                if (depMinutes < nowMinutes - 720) {
                    depMinutes += 24 * 60;
                }
                
                minutesUntil = depMinutes - nowMinutes;
                
                // Sanity check - if result seems wrong, try next day
                if (minutesUntil < -60) {
                    // More than an hour in the past - probably wrong, try next day
                    depMinutes += 24 * 60;
                    minutesUntil = depMinutes - nowMinutes;
                }
                
                return;
            }
        }
    }
    
    // Fallback: try to parse as simple HH:MM format
    if (actualTimeStr.length() >= 5 && actualTimeStr.indexOf(':') > 0) {
        int colonPos = actualTimeStr.indexOf(':');
        int depHour = actualTimeStr.substring(0, colonPos).toInt();
        int depMin = actualTimeStr.substring(colonPos + 1, colonPos + 3).toInt();
        
        char timeBuf[6];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", depHour, depMin);
        displayTime = String(timeBuf);
        
        int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        int depMinutes = depHour * 60 + depMin;
        
        if (depMinutes < nowMinutes - 720) {
            depMinutes += 24 * 60;
        }
        
        minutesUntil = depMinutes - nowMinutes;
        return;
    }
    
    // No valid time string
    displayTime = "??:??";
    minutesUntil = 999;  // Don't filter out - might be a parsing issue
}

bool NextbusAPIClient::isValidDestination(const String& destination, Direction dir) {
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
            DEBUG_PRINTF("Direction match: '%s' contains '%s'\n", lower.c_str(), targets[i]);
            return true;
        }
    }
    
    DEBUG_PRINTF("Direction NO MATCH: '%s' does not match any target for direction %s\n", 
                lower.c_str(), dir == TO_CHELTENHAM ? "TO_CHELTENHAM" : "TO_CHURCHDOWN");
    return false;
}

bool NextbusAPIClient::isTargetRoute(const String& route) {
    for (int i = 0; i < routeCount; i++) {
        if (route == targetRoutes[i]) {
            return true;
        }
    }
    return false;
}

