# 🚌 Bus Timetable E-Ink Display

A beautiful, high-contrast bus departure display for the **LilyGo T5 4.7" E-Paper** (960×540 pixels).

Designed specifically for **elderly users** with:
- Extra-large fonts
- High contrast black/white display
- Clear visual hierarchy
- Simple "Leave in X minutes" countdown

![Bus Timetable Display](docs/display-preview.png)

## ✨ Features

- **Live Bus Departures** - Real-time data from TransportAPI
- **Walking Time Calculation** - Shows when to leave based on distance to stop
- **Home Assistant Integration** - Auto-discovery via MQTT
- **OTA Updates** - Via web interface or GitHub releases
- **Battery Monitoring** - With low-battery warnings
- **Direction Toggle** - Switch between Cheltenham ↔ Churchdown

## 📋 Hardware Requirements

- LilyGo T5 4.7" E-Paper Display (ESP32 + 960×540 e-ink)
- USB-C cable for initial programming
- WiFi network

## 🚀 Quick Start

### 1. Install PlatformIO

Install [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) in Visual Studio Code.

### 2. Configure Your Settings

Copy `src/secrets.example.h` to `src/secrets.h` and fill in your WiFi credentials, API keys, and MQTT settings. The real `src/secrets.h` is ignored by git so you can keep it private.

Edit `include/config.h`:

```cpp
// WiFi
#define WIFI_SSID "YourWiFiName"
#define WIFI_PASSWORD "YourWiFiPassword"

// MQTT (for Home Assistant)
#define MQTT_SERVER "192.168.1.100"  // Your HA IP
#define MQTT_USER "your_mqtt_user"
#define MQTT_PASSWORD "your_mqtt_pass"

// GitHub OTA (optional)
#define OTA_GITHUB_USER "your-github-username"
#define OTA_GITHUB_REPO "bus-timetable-eink"
```

### 3. Build & Upload

```bash
# Build the project
pio run

# Upload to device
pio run -t upload

# Monitor serial output
pio device monitor
```

## 📐 Layout Table

All coordinates are in pixels with the screen origin at the top-left corner. A 10 px margin is enforced on every side—no drawing occurs outside the bounds below.

| Region | X | Y | Width | Height | Notes |
|--------|---|---|-------|--------|-------|
| Hero Background | 10 | 10 | 940 | 60 | Landscape banner containing time, direction, and battery icon |
| Hero – Time Column | 20 | 10 | 140 | 60 | Current time left-aligned, vertically centered |
| Hero – Direction Column | 172 | 10 | 616 | 60 | Direction text centered horizontally, aligned to same baseline |
| Hero – Battery Column | 800 | 10 | 140 | 60 | Battery symbol only (48×27), vertically centered |
| Card Stack Area | 10 | 80 | 940 | 450 | Space dedicated to the departures stack |
| Card 1 | 10 | 80 | 940 | 142 | First departure card |
| Card 2 | 10 | 234 | 940 | 142 | Second departure card (12 px spacing above) |
| Card 3 | 10 | 388 | 940 | 142 | Third departure card, ending 10 px above the bottom margin |

Cards always occupy these rectangles, so typography and wrapped text remain centered inside each box.

## 🏠 Home Assistant Integration

The device automatically registers with Home Assistant via MQTT Discovery.

### Available Entities

| Entity | Type | Description |
|--------|------|-------------|
| Battery | Sensor | Battery percentage |
| Battery Voltage | Sensor | Battery voltage (V) |
| WiFi Signal | Sensor | RSSI (dBm) |
| Direction | Sensor | Current bus direction |
| Buses Displayed | Sensor | Number of buses shown |
| Refresh Display | Button | Force refresh |
| Toggle Direction | Button | Switch direction |

### Example Automation

```yaml
automation:
  - alias: "Notify when bus is 5 minutes away"
    trigger:
      - platform: state
        entity_id: sensor.bus_timetable_display_direction
    action:
      - service: notify.mobile_app
        data:
          message: "Time to leave for the bus!"
```

## 🔄 OTA Updates

### Web Interface

1. Navigate to `http://<device-ip>/update`
2. Select your `.bin` firmware file
3. Click "Update"

### GitHub Releases

1. Create a GitHub release with your `.bin` file attached
2. Set `OTA_GITHUB_USER` and `OTA_GITHUB_REPO` in config.h
3. Device checks for updates every hour

## 📡 MQTT Topics

| Topic | Description |
|-------|-------------|
| `bus_timetable/state` | JSON state (battery, direction, etc.) |
| `bus_timetable/availability` | Online/offline status |
| `bus_timetable/command` | Commands: `refresh`, `toggle_direction`, `reboot` |

### Example State JSON

```json
{
  "battery_percent": 85,
  "battery_voltage": 4.05,
  "rssi": -45,
  "direction": "Cheltenham Spa",
  "bus_count": 3,
  "ip_address": "192.168.1.50",
  "version": "1.0.0"
}
```

## 🔧 Customization

### Change Bus Stops

Edit `include/config.h` to modify stop codes:

```cpp
#define STOP_HARE_HOUNDS "1600GL1187"
#define STOP_ST_JOHNS "1600GLA577"
// Add more stops as needed
```

### Adjust Walking Times

```cpp
#define WALK_TIME_HARE_HOUNDS 12  // 12 minutes walk
#define WALK_TIME_ST_JOHNS 5      // 5 minutes walk
```

### Display Settings

```cpp
#define DISPLAY_FULL_REFRESH_INTERVAL 3600000  // Full refresh every hour
#define DISPLAY_PARTIAL_REFRESH_INTERVAL 60000 // Update every minute
```

## 🐛 Troubleshooting

### Display shows "No WiFi"
- Check SSID and password in config.h
- Ensure WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)

### Buses not appearing
- Verify TransportAPI credentials
- Check if stops are served by routes 94, 95, 97, 98
- Monitor serial output for API errors

### Home Assistant not discovering device
- Verify MQTT broker is running
- Check MQTT credentials
- Look for `homeassistant/sensor/bus_timetable_*` topics

### Display has ghosting
- Full refresh happens automatically every hour
- Force refresh via HA button or wait for next cycle

## 📝 License

MIT License - feel free to modify and share!

## 🙏 Acknowledgments

- [LilyGo](https://github.com/Xinyuan-LilyGO) for the EPD47 library
- [TransportAPI](https://www.transportapi.com/) for bus data
- [Home Assistant](https://www.home-assistant.io/) community
