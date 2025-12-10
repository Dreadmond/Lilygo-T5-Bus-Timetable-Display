#!/bin/bash
# Test script to verify bus stop codes with Traveline Nextbus API

API_BASE="http://nextbus.mxdata.co.uk/nextbuses/1.0/1"
USERNAME="TravelineAPI743"
PASSWORD="Chex0Ohy"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=============================================================================="
echo "Traveline Nextbus API - Bus Stop Verification"
echo "=============================================================================="
echo "API Endpoint: $API_BASE"
echo "Username: $USERNAME"
echo "Testing at: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# Function to get current timestamp in ISO 8601 format
get_timestamp() {
    date -u +"%Y-%m-%dT%H:%M:%SZ"
}

# Function to build SIRI request XML
build_request() {
    local atcocode=$1
    local message_id=${2:-1}
    local timestamp=$(get_timestamp)
    
    cat <<EOF
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Siri version="1.0" xmlns="http://www.siri.org.uk/">
    <ServiceRequest>
        <RequestTimestamp>${timestamp}</RequestTimestamp>
        <RequestorRef>${USERNAME}</RequestorRef>
        <StopMonitoringRequest version="1.0">
            <RequestTimestamp>${timestamp}</RequestTimestamp>
            <MessageIdentifier>${message_id}</MessageIdentifier>
            <MonitoringRef>${atcocode}</MonitoringRef>
        </StopMonitoringRequest>
    </ServiceRequest>
</Siri>
EOF
}

# Function to test a stop
test_stop() {
    local stop_code=$1
    local stop_name=$2
    local expected_routes=$3
    
    echo "=============================================================================="
    echo "Testing: $stop_name"
    echo "Stop Code: $stop_code"
    echo "Expected Routes: $expected_routes"
    echo "=============================================================================="
    
    # Build request
    local request_xml=$(build_request "$stop_code")
    
    # Make API request
    local response=$(curl -s -w "\n%{http_code}" \
        -X POST \
        -u "${USERNAME}:${PASSWORD}" \
        -H "Content-Type: application/xml" \
        -d "$request_xml" \
        "$API_BASE" \
        --max-time 15)
    
    # Extract HTTP status code (last line)
    local http_code=$(echo "$response" | tail -n1)
    local body=$(echo "$response" | sed '$d')
    
    echo "HTTP Status: $http_code"
    
    if [ "$http_code" != "200" ]; then
        echo -e "${RED}ERROR: API returned status $http_code${NC}"
        echo "Response (first 500 chars):"
        echo "$body" | head -c 500
        echo ""
        return 1
    fi
    
    # Check if response contains MonitoredStopVisit
    if ! echo "$body" | grep -q "MonitoredStopVisit"; then
        echo -e "${YELLOW}WARNING: No departures found (may be no buses running)${NC}"
        echo ""
        return 0
    fi
    
    # Extract route numbers
    local routes=$(echo "$body" | grep -oP '(?<=<PublishedLineName>)[^<]+' | sort -u | tr '\n' ', ' | sed 's/,$//')
    
    if [ -z "$routes" ]; then
        echo -e "${YELLOW}WARNING: No routes found in response${NC}"
    else
        echo "Routes found: $routes"
        
        # Check for target routes (94-98)
        local has_target=false
        for route in 94 95 96 97 98; do
            if echo "$routes" | grep -q "\b$route\b"; then
                has_target=true
                break
            fi
        done
        
        if [ "$has_target" = true ]; then
            echo -e "${GREEN}✓ Target routes (94-98) found${NC}"
        else
            echo -e "${RED}✗ WARNING: No target routes (94-98) found!${NC}"
        fi
    fi
    
    # Show some departure details
    echo ""
    echo "Sample departures:"
    echo "---"
    
    # Extract first few departures
    local count=0
    while IFS= read -r line; do
        if [ $count -ge 3 ]; then
            break
        fi
        
        local route=$(echo "$line" | grep -oP '(?<=<PublishedLineName>)[^<]+')
        local direction=$(echo "$line" | grep -oP '(?<=<DirectionName>)[^<]+')
        local aimed=$(echo "$line" | grep -oP '(?<=<AimedDepartureTime>)[^<]+')
        local expected=$(echo "$line" | grep -oP '(?<=<ExpectedDepartureTime>)[^<]+')
        
        if [ -n "$route" ]; then
            local time_str=""
            local status=""
            if [ -n "$expected" ]; then
                time_str=$(echo "$expected" | cut -d'T' -f2 | cut -d'.' -f1 | cut -d'+' -f1 | cut -d'-' -f1)
                status="LIVE"
            elif [ -n "$aimed" ]; then
                time_str=$(echo "$aimed" | cut -d'T' -f2 | cut -d'.' -f1 | cut -d'+' -f1 | cut -d'-' -f1)
                status="SCHEDULED"
            fi
            
            if [ -n "$time_str" ]; then
                time_str=$(echo "$time_str" | cut -d':' -f1-2)
                echo "  Route $route: $time_str → $direction ($status)"
                count=$((count + 1))
            fi
        fi
    done < <(echo "$body" | awk '/<MonitoredStopVisit>/,/<\/MonitoredStopVisit>/')
    
    echo ""
    return 0
}

# Test Cheltenham direction stops
echo "=============================================================================="
echo "TESTING CHELTENHAM DIRECTION STOPS"
echo "=============================================================================="

test_stop "1600GLA569" "Churchdown Library" "96, 97"
test_stop "1600GL1187" "Hare & Hounds" "94"
test_stop "1600GLA577" "St John's Church" "98"

# Test Churchdown direction stops
echo "=============================================================================="
echo "TESTING CHURCHDOWN DIRECTION STOPS"
echo "=============================================================================="

test_stop "1600GLA36692" "Promenade (Stop 3)" "94-98"
test_stop "1600GL1076" "Promenade (Stop 5)" "94-98"

echo "=============================================================================="
echo "Testing complete!"
echo "=============================================================================="

