#!/usr/bin/env python3
"""
Test script to check if route 98 shows live data
"""

import requests
from datetime import datetime, timezone
from xml.etree import ElementTree as ET
import time

# API Configuration
API_BASE = "http://nextbus.mxdata.co.uk/nextbuses/1.0/1"
USERNAME = "TravelineAPI743"
PASSWORD = "Chex0Ohy"

# Stops where route 98 should appear
STOPS_WITH_98 = [
    {"code": "1600GLA577", "name": "St John's Church"},
    {"code": "1600GL1076", "name": "Promenade (Stop 5)"},
]

def get_current_timestamp():
    """Get current timestamp in ISO 8601 format"""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

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
    """Parse SIRI-SM XML response and extract route 98 departures"""
    try:
        root = ET.fromstring(xml_response)
        ns = {'siri': 'http://www.siri.org.uk/'}
        
        delivery = root.find('.//siri:StopMonitoringDelivery', ns)
        if delivery is None:
            return []
        
        route98_departures = []
        
        for visit in delivery.findall('.//siri:MonitoredStopVisit', ns):
            journey = visit.find('.//siri:MonitoredVehicleJourney', ns)
            if journey is None:
                continue
            
            line_name_elem = journey.find('.//siri:PublishedLineName', ns)
            route = line_name_elem.text if line_name_elem is not None else ""
            
            # Only look at route 98
            if route != "98":
                continue
            
            direction_elem = journey.find('.//siri:DirectionName', ns)
            direction = direction_elem.text if direction_elem is not None else "Unknown"
            
            call = journey.find('.//siri:MonitoredCall', ns)
            if call is None:
                continue
            
            aimed_time_elem = call.find('.//siri:AimedDepartureTime', ns)
            expected_time_elem = call.find('.//siri:ExpectedDepartureTime', ns)
            
            aimed_time = aimed_time_elem.text if aimed_time_elem is not None else None
            expected_time = expected_time_elem.text if expected_time_elem is not None else None
            
            route98_departures.append({
                'direction': direction,
                'aimed_time': aimed_time,
                'expected_time': expected_time,
                'is_live': expected_time is not None
            })
        
        return route98_departures
    except Exception as e:
        print(f"Error parsing response: {e}")
        return None

def test_route98_at_stop(stop_code, stop_name):
    """Test route 98 at a specific stop"""
    print(f"\n{'='*70}")
    print(f"Testing Route 98 at: {stop_name} ({stop_code})")
    print(f"{'='*70}")
    
    request_xml = build_siri_request(stop_code)
    
    try:
        response = requests.post(
            API_BASE,
            data=request_xml,
            auth=(USERNAME, PASSWORD),
            headers={'Content-Type': 'application/xml'},
            timeout=15
        )
        
        if response.status_code != 200:
            print(f"ERROR: HTTP {response.status_code}")
            return
        
        departures = parse_siri_response(response.text)
        
        if departures is None:
            print("ERROR: Failed to parse response")
            return
        
        if len(departures) == 0:
            print("No route 98 departures found at this stop")
            return
        
        print(f"\nFound {len(departures)} route 98 departure(s):")
        print("-" * 70)
        
        live_count = 0
        scheduled_count = 0
        
        for i, dep in enumerate(departures, 1):
            time_str = dep['expected_time'] if dep['is_live'] else dep['aimed_time']
            
            if time_str:
                try:
                    dt = datetime.fromisoformat(time_str.replace('Z', '+00:00'))
                    time_display = dt.strftime("%H:%M")
                except:
                    time_display = time_str[:5] if len(time_str) >= 5 else time_str
            else:
                time_display = "??:??"
            
            status = "LIVE" if dep['is_live'] else "SCHEDULED"
            if dep['is_live']:
                live_count += 1
            else:
                scheduled_count += 1
            
            print(f"  {i}. {time_display} → {dep['direction']} ({status})")
            if dep['is_live']:
                print(f"     Scheduled: {dep['aimed_time']}")
                print(f"     Expected:  {dep['expected_time']}")
        
        print(f"\nSummary: {live_count} LIVE, {scheduled_count} SCHEDULED")
        
        if live_count > 0:
            print(f"✓ Route 98 DOES show live data at this stop!")
        else:
            print(f"✗ Route 98 only shows scheduled data at this stop")
        
    except Exception as e:
        print(f"ERROR: {e}")
        import traceback
        traceback.print_exc()

def main():
    print("=" * 70)
    print("Route 98 Live Data Test")
    print("=" * 70)
    print(f"Testing at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    
    for stop in STOPS_WITH_98:
        test_route98_at_stop(stop["code"], stop["name"])
        time.sleep(1)  # Small delay between requests
    
    print("\n" + "=" * 70)
    print("Note: Live data availability depends on:")
    print("  - Whether the bus has GPS tracking enabled")
    print("  - Whether the bus is currently in service")
    print("  - Time of day (more live data during service hours)")
    print("=" * 70)

if __name__ == "__main__":
    main()

