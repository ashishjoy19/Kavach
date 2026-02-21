/*
 * Kavach gas sensor node
 * Reads analog gas sensor (e.g. MQ-2 on ADC pin). When value exceeds threshold,
 * publishes to fabacademy/kavach/gas so the Kavach device shows gas leak alert.
 *
 * Hardware: ESP32 (or ESP8266), gas sensor analog out → e.g. GPIO34 (ESP32 ADC1).
 * Libraries: WiFi, PubSubClient (Arduino IDE) or ESP-IDF (adjust if needed).
 *
 * Set WIFI_SSID, WIFI_PASS, MQTT_BROKER and GAS_PIN / LEAK_THRESHOLD below.
 */

#include <WiFi.h>
#include <PubSubClient.h>

// --- Configure these ---
#define WIFI_SSID     "your_ssid"
#define WIFI_PASS     "your_password"
#define MQTT_BROKER   "192.168.1.100"   // Broker IP (same as Kavach)
#define MQTT_PORT     1883

#define GAS_PIN       34                 // Analog pin for gas sensor (ESP32: 32-39)
#define LEAK_THRESHOLD 600                // Above this → publish LEAK (tune for your sensor)
#define POLL_MS       2000               // Check every 2 s
#define MQTT_TOPIC_GAS "fabacademy/kavach/gas"

WiFiClient wifi;
PubSubClient mqtt(wifi);

void setup_wifi() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Gas sensor node starting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  (void)payload;
  (void)length;
}

void mqtt_reconnect() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting... ");
    if (mqtt.connect("kavach_gas_sensor")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(mqtt.state());
      delay(3000);
    }
  }
}

void setup() {
  setup_wifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
  pinMode(GAS_PIN, INPUT);
}

void loop() {
  if (!mqtt.connected()) {
    mqtt_reconnect();
  }
  mqtt.loop();

  int raw = analogRead(GAS_PIN);
  if (raw >= LEAK_THRESHOLD) {
    // Publish JSON expected by Kavach: must contain "LEAK" in payload
    char payload[80];
    snprintf(payload, sizeof(payload),
             "{\"device\":\"gas_sensor\",\"gas\":%d,\"state\":\"LEAK\"}", raw);
    if (mqtt.publish(MQTT_TOPIC_GAS, payload)) {
      Serial.printf("LEAK published: %s\n", payload);
    }
    delay(5000);  // Throttle alerts
  }

  delay(POLL_MS);
}
