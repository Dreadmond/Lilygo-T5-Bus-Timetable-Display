#!/usr/bin/env python3
"""
Test script to verify bus stop codes with Traveline Nextbus API
Tests all configured stops and shows available routes/departures
"""

import requests
from datetime import datetime
from xml.etree import ElementTree as ET
import sys

# API Configuration
API_BASE = "http://nextbus.mxdata.co.uk/nextbuses/1.0/1"
USERNAME = "TravelineAPI743"
PASSWORD = "Chex0Ohy"

# Bus stops from config.h
STOPS = {
    "TO_CHELTENHAM": [
        {"code": "1600GLA569", "name": "Churchdown Library", "expected_routes": ["96", "97"]},
        {"code": "1600GL1187", "name": "Hare & Hounds", "expected_routes": ["94"]},
        {"code": "1600GLA577", "name": "St John's Church", "expected_routes": ["98"]},
    ],
    "TO_CHURCHDOWN": [
        {"code": "1600GLA36692", "name": "Promenade (Stop 3)", "expected_routes": ["94", "95", "96", "97", "98"]},
        {"code": "1600GL1076", "name": "Promenade (Stop 5)", "expected_routes": ["94", "95", "96", "97", "98"]},
    ]
}

TARGET_ROUTES = ["94", "95", "96", "97", "98"]


def get_current_timestamp():
    """Get current timestamp in ISO 8601 format"""
    return datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")


def build_siri_request(atcocode, message_id=1):
    """Build SIRI-SM XML request"""
    timestamp = get_current_timestamp()
    
    xml = f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Siri version="1.0" xmlns="http://www.siri.org.uk/">
    <ServiceRequest>
        <RequestTimestamp>{timestamp}</RequestTimestamp>
        <RequestorRef>{USERNAME}</RequestorRef>
        <StopMonitoringRequest version="1.0">
            <RequestTimestamp>{timestamp}</RequestTimestamp>
            <MessageIdentifier>{message_id}</MessageIdentifier>
            <MonitoringRef>{atcocode}</MonitoringRef>
        </StopMonitoringRequest>
    </ServiceRequest>
</Siri>'''
    return xml


def parse_siri_response(xml_response):
    """Parse SIRI-SM XML response and extract departures"""
    try:
        root = ET.fromstring(xml_response)
        
        # Find namespace
        ns = {'siri': 'http://www.siri.org.uk/'}
        
        # Find StopMonitoringDelivery
        delivery = root.find('.//siri:StopMonitoringDelivery', ns)
        if delivery is None:
            return []
        
        departures = []
        
        # Find all MonitoredStopVisit elements
        for visit in delivery.findall('.//siri:MonitoredStopVisit', ns):
            journey = visit.find('.//siri:MonitoredVehicleJourney', ns)
            if journey is None:
                continue
            
            # Extract route number
            line_name_elem = journey.find('.//siri:PublishedLineName', ns)
            route = line_name_elem.text if line_name_elem is not None else "Unknown"
            
            # Extract direction/destination
            direction_elem = journey.find('.//siri:DirectionName', ns)
            direction = direction_elem.text if direction_elem is not None else "Unknown"
            
            # Extract times
            call = journey.find('.//siri:MonitoredCall', ns)
            if call is None:
                continue
            
            aimed_time_elem = call.find('.//siri:AimedDepartureTime', ns)
            expected_time_elem = call.find('.//siri:ExpectedDepartureTime', ns)
            
            aimed_time = aimed_time_elem.text if aimed_time_elem is not None else None
            expected_time = expected_time_elem.text if expected_time_elem is not None else None
            
            departures.append({
                'route': route,
                'direction': direction,
                'aimed_time': aimed_time,
                'expected_time': expected_time,
                'is_live': expected_time is not None
            })
        
        return departures
    except ET.ParseError as e:
        print(f"XML Parse Error: {e}")
        return None
    except Exception as e:
        print(f"Error parsing response: {e}")
        return None


def test_stop(stop_code, stop_name, expected_routes):
    """Test a single bus stop"""
    print(f"\n{'='*70}")
    print(f"Testing: {stop_name}")
    print(f"Stop Code: {stop_code}")
    print(f"Expected Routes: {', '.join(expected_routes)}")
    print(f"{'='*70}")
    
    # Build request
    request_xml = build_siri_request(stop_code)
    
    try:
        # Make API request
        response = requests.post(
            API_BASE,
            data=request_xml,
            auth=(USERNAME, PASSWORD),
            headers={'Content-Type': 'application/xml'},
            timeout=15
        )
        
        print(f"HTTP Status: {response.status_code}")
        
        if response.status_code != 200:
            print(f"ERROR: API returned status {response.status_code}")
            print(f"Response: {response.text[:500]}")
            return False
        
        # Parse response
        departures = parse_siri_response(response.text)
        
        if departures is None:
            print("ERROR: Failed to parse response")
            return False
        
        if len(departures) == 0:
            print("WARNING: No departures found (may be no buses running)")
            return True  # Not an error, just no buses
        
        print(f"\nFound {len(departures)} departure(s):")
        print("-" * 70)
        
        # Group by route
        routes_found = {}
        for dep in departures:
            route = dep['route']
            if route not in routes_found:
                routes_found[route] = []
            routes_found[route].append(dep)
        
        # Show all routes found
        print(f"Routes found: {', '.join(sorted(routes_found.keys()))}")
        
        # Check if target routes are present
        target_routes_found = [r for r in routes_found.keys() if r in TARGET_ROUTES]
        if target_routes_found:
            print(f"✓ Target routes found: {', '.join(target_routes_found)}")
        else:
            print(f"✗ WARNING: No target routes (94-98) found!")
        
        # Show details for each route
        for route in sorted(routes_found.keys()):
            deps = routes_found[route]
            print(f"\n  Route {route}:")
            for dep in deps[:3]:  # Show first 3 departures
                time_str = dep['expected_time'] if dep['is_live'] else dep['aimed_time']
                if time_str:
                    # Extract just the time part
                    try:
                        dt = datetime.fromisoformat(time_str.replace('Z', '+00:00'))
                        time_display = dt.strftime("%H:%M")
                    except:
                        time_display = time_str[:5] if len(time_str) >= 5 else time_str
                else:
                    time_display = "??:??"
                
                status = "LIVE" if dep['is_live'] else "SCHEDULED"
                print(f"    {time_display} → {dep['direction']} ({status})")
        
        return True
        
    except requests.exceptions.RequestException as e:
        print(f"ERROR: Request failed: {e}")
        return False
    except Exception as e:
        print(f"ERROR: Unexpected error: {e}")
        import traceback
        traceback.print_exc()
        return False


def main():
    """Main test function"""
    print("=" * 70)
    print("Traveline Nextbus API - Bus Stop Verification")
    print("=" * 70)
    print(f"API Endpoint: {API_BASE}")
    print(f"Username: {USERNAME}")
    print(f"Testing at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    
    results = {
        "TO_CHELTENHAM": [],
        "TO_CHURCHDOWN": []
    }
    
    # Test Cheltenham direction stops
    print("\n" + "=" * 70)
    print("TESTING CHELTENHAM DIRECTION STOPS")
    print("=" * 70)
    
    for stop in STOPS["TO_CHELTENHAM"]:
        success = test_stop(
            stop["code"],
            stop["name"],
            stop["expected_routes"]
        )
        results["TO_CHELTENHAM"].append({
            "stop": stop["name"],
            "code": stop["code"],
            "success": success
        })
    
    # Test Churchdown direction stops
    print("\n" + "=" * 70)
    print("TESTING CHURCHDOWN DIRECTION STOPS")
    print("=" * 70)
    
    for stop in STOPS["TO_CHURCHDOWN"]:
        success = test_stop(
            stop["code"],
            stop["name"],
            stop["expected_routes"]
        )
        results["TO_CHURCHDOWN"].append({
            "stop": stop["name"],
            "code": stop["code"],
            "success": success
        })
    
    # Summary
    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    
    total_stops = 0
    successful_stops = 0
    
    for direction, stops in results.items():
        print(f"\n{direction}:")
        for result in stops:
            total_stops += 1
            status = "✓" if result["success"] else "✗"
            print(f"  {status} {result['stop']} ({result['code']})")
            if result["success"]:
                successful_stops += 1
    
    print(f"\nTotal: {successful_stops}/{total_stops} stops tested successfully")
    
    if successful_stops == total_stops:
        print("\n✓ All stops are working correctly!")
        return 0
    else:
        print(f"\n✗ {total_stops - successful_stops} stop(s) had issues")
        return 1


if __name__ == "__main__":
    sys.exit(main())



