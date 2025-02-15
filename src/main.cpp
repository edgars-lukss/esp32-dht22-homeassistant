#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "esp_system.h"
#include "esp_task_wdt.h"
#include <Preferences.h>

// Function forward declarations
void setupAutoDiscovery();

// Initialize preferences
Preferences preferences;

// Wi-Fi credentials
const char* ssid = "your_wifi_ssid";
const char* password = "your_wifi_password";

// MQTT Broker details
const char* mqtt_server = "your_mqtt_broker_ip";
const int mqtt_port = 1883;

// Telemetry interval
unsigned long lastTelemetryTime = 0;
unsigned long lastPublishedInterval = 0;
unsigned long telemetryInterval = 60000;

// Flag to track if retained state has been processed
bool retainedStateProcessed = false;

// Version, model and manufacturer
const char* version = "1.0.10";
const char* model = "ESP32_DHT22";
const char* manufacturer = "Edge";

// DHT22 setup
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// MQTT client
WiFiClient espClient;
PubSubClient client(espClient);

// Unique device ID (based on MAC address)
String device_id;

// MQTT topics
String temperature_config_topic;
String humidity_config_topic;
String telemetry_topic;
String command_topic;
String interval_state_topic;

// Friendly names for sensors
const char* temperature_name = "Temperature";
const char* humidity_name = "Humidity";

// Watchdog timeout in seconds
#define WDT_TIMEOUT 20 // 20 seconds

// Function to connect to Wi-Fi
void connectToWiFi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

// Ensure Wi-Fi stays connected
void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    connectToWiFi();
  }
}

void setupTimeSync() {
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");

  Serial.print("Synchronizing time");
  while (time(nullptr) < 100000) { // Wait until time is set
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nTime synchronized!");
}

// Save telemetry interval to NVS
void saveTelemetryIntervalToNVS(unsigned long interval) {
    preferences.begin("settings", false); // Open NVS namespace "settings" for writing
    preferences.putUInt("telemetryInterval", interval); // Save the interval
    preferences.end(); // Close NVS
    Serial.println("Saved telemetry interval to NVS.");
}

// Load telemetry interval from NVS
unsigned long loadTelemetryIntervalFromNVS() {
    preferences.begin("settings", true); // Open NVS namespace "settings" for reading
    unsigned long interval = preferences.getUInt("telemetryInterval", 60000); // Default to 60000 ms
    preferences.end(); // Close NVS
    Serial.print("Loaded telemetry interval from NVS: ");
    Serial.println(interval);
    return interval;
}

void updateTelemetryInterval(unsigned long newInterval) {
    if (newInterval >= 1000 && newInterval <= 600000) {
        if (telemetryInterval != newInterval) {
            telemetryInterval = newInterval;
            saveTelemetryIntervalToNVS(newInterval); // Save to NVS
            Serial.print("Telemetry interval updated to: ");
            Serial.print(telemetryInterval);
            Serial.println(" ms");

            if (telemetryInterval != lastPublishedInterval) {
                client.publish(interval_state_topic.c_str(), String(telemetryInterval).c_str(), true);
                lastPublishedInterval = telemetryInterval;
            }
        }
    } else {
        Serial.println("Invalid telemetry interval received!");
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);

  // Handle retained telemetry interval state
  if (String(topic) == interval_state_topic) {
    Serial.println("Processing retained telemetry interval state...");
    unsigned long retainedInterval = message.toInt();
    if (retainedInterval >= 1000 && retainedInterval <= 600000) {
      telemetryInterval = retainedInterval;
      lastPublishedInterval = retainedInterval;
      retainedStateProcessed = true; // Mark retained state as processed
      Serial.print("Telemetry interval updated from retained state: ");
      Serial.print(telemetryInterval);
      Serial.println(" ms");
    } else {
      Serial.println("Invalid retained telemetry interval received!");
    }
    return;
  }

  // Handle direct command to update telemetry interval
  if (String(topic) == command_topic) {
    Serial.println("Processing telemetry interval update command...");
    unsigned long newInterval = message.toInt();
    if (newInterval >= 1000 && newInterval <= 600000) {
      telemetryInterval = newInterval;
      Serial.print("Telemetry interval updated to: ");
      Serial.print(telemetryInterval);
      Serial.println(" ms");

      // Publish updated interval state with retain flag
      if (telemetryInterval != lastPublishedInterval) {
        client.publish(interval_state_topic.c_str(), String(telemetryInterval).c_str(), true);
        lastPublishedInterval = telemetryInterval;
        Serial.println("Published updated telemetry interval to MQTT.");
      }
    } else {
      Serial.println("Invalid telemetry interval received!");
    }
    return;
  }

  Serial.println("Unhandled topic received.");
}

void reconnectMQTT() {
  static unsigned long lastReconnectAttempt = 0;
  unsigned long now = millis();

  if (!client.connected() && (now - lastReconnectAttempt > 5000)) { // Retry every 5 seconds
    Serial.println("Attempting MQTT connection...");
    lastReconnectAttempt = now;

    if (client.connect(device_id.c_str(), 
                        ("homeassistant/sensor/" + device_id + "/availability").c_str(), 
                        0, 
                        true, 
                        "offline")) {
      Serial.println("MQTT connected");

      // Publish availability as online
      client.publish(("homeassistant/sensor/" + device_id + "/availability").c_str(), "online", true);

      // Resubscribe to topics
      client.subscribe(command_topic.c_str());
      client.subscribe(interval_state_topic.c_str());
      Serial.println("Resubscribed to MQTT topics.");

      // Reset retained state flag
      retainedStateProcessed = false;

      // Resend discovery topics
      setupAutoDiscovery();
    } else {
      Serial.print("MQTT connection failed, rc=");
      Serial.println(client.state());
    }
  }
}

// Function to send auto-discovery messages to Home Assistant
void setupAutoDiscovery() {
  // Temperature sensor config
  JsonDocument tempDoc = JsonDocument();
  tempDoc["name"] = "Temperature";
  tempDoc["uniq_id"] = device_id + "_temperature";
  tempDoc["stat_t"] = telemetry_topic;
  tempDoc["val_tpl"] = "{{ value_json.temperature }}";
  tempDoc["unit_of_meas"] = "°C";
  tempDoc["dev_cla"] = "temperature";
  tempDoc["stat_cla"] = "measurement";
  tempDoc["avty_t"] = "homeassistant/sensor/" + device_id + "/availability";
  tempDoc["json_attr_t"] = telemetry_topic; // Add full telemetry topic
  tempDoc["json_attr_tpl"] = "{{ value_json | tojson }}"; // Serialize JSON attributes

  JsonObject tempDevice = tempDoc["dev"].to<JsonObject>();
  tempDevice["ids"] = device_id;
  tempDevice["name"] = device_id;
  tempDevice["sw"] = version;
  tempDevice["mdl"] = model;
  tempDevice["mf"] = manufacturer;

  String tempPayload;
  serializeJson(tempDoc, tempPayload);
  client.publish(temperature_config_topic.c_str(), tempPayload.c_str(), true);

  // Humidity sensor config
  JsonDocument humDoc = JsonDocument();
  humDoc["name"] = "Humidity";
  humDoc["uniq_id"] = device_id + "_humidity";
  humDoc["stat_t"] = telemetry_topic;
  humDoc["val_tpl"] = "{{ value_json.humidity }}";
  humDoc["unit_of_meas"] = "%";
  humDoc["dev_cla"] = "humidity";
  humDoc["stat_cla"] = "measurement";
  humDoc["avty_t"] = "homeassistant/sensor/" + device_id + "/availability";
  humDoc["json_attr_t"] = telemetry_topic; // Add full telemetry topic
  humDoc["json_attr_tpl"] = "{{ value_json | tojson }}"; // Serialize JSON attributes

  JsonObject humDevice = humDoc["dev"].to<JsonObject>();
  humDevice["ids"] = device_id;
  humDevice["name"] = device_id;
  humDevice["sw"] = version;
  humDevice["mdl"] = model;
  humDevice["mf"] = manufacturer;

  String humPayload;
  serializeJson(humDoc, humPayload);
  client.publish(humidity_config_topic.c_str(), humPayload.c_str(), true);

  // Telemetry interval configuration
  JsonDocument intervalDoc = JsonDocument();
  intervalDoc["name"] = "Telemetry Interval";
  intervalDoc["uniq_id"] = device_id + "_telemetry_interval";
  intervalDoc["cmd_t"] = command_topic;
  intervalDoc["stat_t"] = interval_state_topic;
  intervalDoc["val_tpl"] = "{{ value }}";
  intervalDoc["min"] = 1000;
  intervalDoc["max"] = 600000;
  intervalDoc["step"] = 1000;
  intervalDoc["unit_of_meas"] = "ms";
  intervalDoc["json_attr_t"] = telemetry_topic; // Add full telemetry topic
  intervalDoc["json_attr_tpl"] = "{{ value_json | tojson }}"; // Serialize JSON attributes

  JsonObject intervalDevice = intervalDoc["dev"].to<JsonObject>();
  intervalDevice["ids"] = device_id;
  intervalDevice["name"] = device_id;
  intervalDevice["sw"] = version;
  intervalDevice["mdl"] = model;
  intervalDevice["mf"] = manufacturer;

  String intervalPayload;
  serializeJson(intervalDoc, intervalPayload);
  client.publish(("homeassistant/number/" + device_id + "/telemetry_interval/config").c_str(), intervalPayload.c_str(), true);

  // Publish availability topic
  client.publish(("homeassistant/sensor/" + device_id + "/availability").c_str(), "online", true);

  // Publish initial telemetry interval state only if retained state is not yet processed
  if (!retainedStateProcessed && telemetryInterval != lastPublishedInterval) {
      client.publish(interval_state_topic.c_str(), String(telemetryInterval).c_str(), true);
      lastPublishedInterval = telemetryInterval;
      Serial.println("Published default telemetry interval as retained state was not received.");
  }


  // Debugging
  Serial.println("Auto-discovery messages sent:");
  Serial.println("Temperature Config:");
  serializeJsonPretty(tempDoc, Serial);
  Serial.println("\nHumidity Config:");
  serializeJsonPretty(humDoc, Serial);
  Serial.println("\nTelemetry Interval Config:");
  serializeJsonPretty(intervalDoc, Serial);
}

String getCurrentTimestamp() {
  time_t now = time(nullptr);
  struct tm timeInfo;
  gmtime_r(&now, &timeInfo); // Get time in UTC
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeInfo);
  return String(buffer);
}

int calculateWiFiQuality(int rssi) {
  // Define the range of RSSI values
  const int rssiMin = -100; // Worst signal strength
  const int rssiMax = -50;  // Best signal strength

  // Clamp RSSI to the valid range
  if (rssi <= rssiMin) return 0;     // 0% quality
  if (rssi >= rssiMax) return 100;   // 100% quality

  // Calculate quality as a percentage
  return (rssi - rssiMin) * 100 / (rssiMax - rssiMin);
}

void publishTelemetry(float temperature, float humidity) {
  JsonDocument doc = JsonDocument();

  doc["temperature"] = temperature; // Add temperature value
  doc["humidity"] = humidity;       // Add humidity value
  doc["wifi_quality"] = calculateWiFiQuality(WiFi.RSSI()); // Add Wi-Fi signal strength
  doc["timestamp"] = getCurrentTimestamp(); // Add timestamp
  doc["ip_address"] = WiFi.localIP().toString(); // Add IP address

  String payload;
  serializeJson(doc, payload); // Serialize JSON to string

  if (client.publish(telemetry_topic.c_str(), payload.c_str())) {
    Serial.println("Telemetry published successfully.");
  } else {
    Serial.println("Failed to publish telemetry. Retrying...");
    delay(1000);
    client.publish(telemetry_topic.c_str(), payload.c_str());
  }

  Serial.print("Published telemetry: ");
  Serial.println(payload);
}

// Setup OTA
void setupOTA() {
  ArduinoOTA.setHostname(device_id.c_str());
  ArduinoOTA.setPassword("<Your_Password_here>");

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA Start: " + type);

    // Disable WDT during OTA update
    esp_task_wdt_delete(NULL);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");

    // Re-enable WDT after OTA update
    esp_task_wdt_add(NULL);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    esp_task_wdt_reset(); // Reset WDT during OTA progress
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

  ArduinoOTA.begin();
  Serial.println("OTA Ready");
}

void setup() {
  Serial.begin(115200);
  Serial.println("DHT22 with Wi-Fi and MQTT");

  dht.begin();
  connectToWiFi();
  setupTimeSync();

  // Generate device ID
  device_id = "esp32_" + WiFi.macAddress();
  device_id.replace(":", "");

  // Load telemetry interval from NVS
  telemetryInterval = loadTelemetryIntervalFromNVS();
  lastPublishedInterval = telemetryInterval; // Sync with the last published state


  // Set up MQTT topics
  temperature_config_topic = "homeassistant/sensor/" + device_id + "/temperature/config";
  humidity_config_topic = "homeassistant/sensor/" + device_id + "/humidity/config";
  telemetry_topic = "homeassistant/sensor/" + device_id + "/telemetry";
  command_topic = "homeassistant/sensor/" + device_id + "/command";
  interval_state_topic = "homeassistant/sensor/" + device_id + "/interval/state";

  // Initialize MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  client.setBufferSize(2048); // Adjust buffer for payload size, default 1024
  client.setKeepAlive(60);    // 60 seconds keep-alive interval

  reconnectMQTT(); // Attempt connection

  // Initialize watchdog timer
  esp_task_wdt_init(WDT_TIMEOUT, true); // Enable panic on WDT timeout
  esp_task_wdt_add(NULL);               // Add current thread to WDT

  setupAutoDiscovery();
  setupOTA();
}

void loop() {
  ensureWiFiConnected();

  if (!client.connected()) {
    reconnectMQTT();
  }

  client.loop();
  ArduinoOTA.handle();

  // Feed the watchdog
  esp_task_wdt_reset();

  unsigned long now = millis();
  if (now - lastTelemetryTime >= telemetryInterval) {
    lastTelemetryTime = now;

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (!isnan(humidity) && !isnan(temperature)) {
      publishTelemetry(temperature, humidity);
    } else {
      Serial.println("Failed to read from DHT sensor!");
    }
  }
}
