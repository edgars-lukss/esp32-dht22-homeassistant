# ESP32 DHT22 Sensor with Home Assistant Auto Discovery

This project enables an ESP32 to interface with a DHT22 temperature and humidity sensor and send data to Home Assistant via MQTT. It implements Home Assistant's Auto Discovery feature, making sensor integration seamless.

## Features
- **Wi-Fi Connectivity**: Connects to a specified Wi-Fi network.
- **MQTT Integration**: Publishes sensor data to an MQTT broker.
- **Home Assistant Auto Discovery**: Automatically configures sensors in Home Assistant.
- **OTA Updates**: Supports over-the-air firmware updates.
- **Persistent Telemetry Interval**: Saves and restores interval settings from ESP32's NVS storage.
- **Wi-Fi Signal Quality Reporting**: Sends Wi-Fi signal strength along with sensor data.
- **Watchdog Timer**: Ensures the ESP32 does not hang indefinitely.

## Hardware Requirements
- ESP32 Board
- DHT22 Temperature & Humidity Sensor
- MQTT Broker (e.g., Mosquitto)
- Home Assistant instance with MQTT integration

## Software Requirements
- Arduino IDE / PlatformIO
- Required Libraries:
  - `WiFi.h`
  - `PubSubClient.h`
  - `Adafruit_Sensor.h`
  - `DHT.h`
  - `DHT_U.h`
  - `ArduinoJson.h`
  - `ArduinoOTA.h`
  - `Preferences.h`

## Installation
### 1. Clone Repository
```sh
git clone https://github.com/edgars-lukss/esp32-dht22-homeassistant.git
cd esp32-dht22-homeassistant
```

### 2. Configure Wi-Fi & MQTT
Update the following details in the source code:
```cpp
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";
const char* mqtt_server = "your_mqtt_broker_ip";
const int mqtt_port = 1883;
```

### 3. PlatformIO Configuration
For PlatformIO users, ensure you have the following configuration in your `platformio.ini` file:
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.f_cpu = 240000000L
board_build.partitions = partitions.csv
board_build.flash_mode = dio
board_build.flash_size = 4MB
board_upload.flash_size = 4MB
upload_protocol = espota
upload_port = 192.168.8.49
upload_flags =
    --auth=<Your_Password_here>
lib_deps =
    adafruit/DHT sensor library
    adafruit/Adafruit Unified Sensor
    knolleary/PubSubClient
    bblanchon/ArduinoJson
```

### 4. Upload Code
Compile and upload the code using the Arduino IDE or PlatformIO.

### 5. Home Assistant Configuration
No manual configuration is needed! The device automatically registers sensors via Home Assistant's MQTT auto-discovery feature.

## MQTT Topics
- **Telemetry Data:** `homeassistant/sensor/{device_id}/telemetry`
- **Temperature Config:** `homeassistant/sensor/{device_id}/temperature/config`
- **Humidity Config:** `homeassistant/sensor/{device_id}/humidity/config`
- **Command Topic:** `homeassistant/sensor/{device_id}/command`
- **Telemetry Interval State:** `homeassistant/sensor/{device_id}/interval/state`

## OTA Firmware Updates
The ESP32 supports OTA updates. Upload new firmware via the Arduino IDE or PlatformIO using the configured OTA hostname and password.

## Example MQTT Message (Telemetry)
```json
{
  "temperature": 23.5,
  "humidity": 55.2,
  "wifi_quality": 80,
  "timestamp": "2025-02-15T12:34:56",
  "ip_address": "192.168.1.100"
}
```

## Troubleshooting
- **Wi-Fi Not Connecting?** Check SSID and password.
- **MQTT Connection Fails?** Ensure the broker is running and accessible.
- **Home Assistant Not Detecting Sensor?** Check MQTT broker logs for discovery messages.
- **DHT Sensor Readings Fail?** Verify wiring and power supply.

## License
This project is licensed under the MIT License.

