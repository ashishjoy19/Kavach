/*
 * Kavach PIR (intruder) sensor node
 * When motion is detected, publishes to fabacademy/kavach/intruder. The Kavach
 * device subscribes and shows an intruder/motion alert.
 *
 * Hardware: ESP32/ESP8266, PIR sensor (e.g. HC-SR501) output → PIR_PIN.
 * Libraries: WiFi, PubSubClient.
 *
 * Set WIFI_SSID, WIFI_PASS, MQTT_BROKER and PIR_PIN. Tune COOLDOWN_MS to avoid
 * flooding (PIR often stays HIGH for a few seconds).
 */

#include <WiFi.h>
#include <PubSubClient.h>

// --- Configure these ---
#define WIFI_SSID     "your_ssid"
#define WIFI_PASS     "your_password"
#define MQTT_BROKER   "192.168.1.100"
#define MQTT_PORT     1883

#define PIR_PIN       4   // GPIO connected to PIR output (HIGH = motion)
#define MQTT_TOPIC_INTRUDER "fabacademy/kavach/intruder"
#define COOLDOWN_MS   5000  // Min time between two published alerts (PIR cooldown)

WiFiClient wifi;
PubSubClient mqtt(wifi);

unsigned long lastPublish = 0;

void setup_wifi() {
  Serial.begin(115200);
  delay(100);
  Serial.println("PIR sensor node starting");
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
    if (mqtt.connect("kavach_pir_sensor")) {
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
  pinMode(PIR_PIN, INPUT);
}

void loop() {
  if (!mqtt.connected()) {
    mqtt_reconnect();
  }
  mqtt.loop();

  // PIR output: HIGH when motion detected
  if (digitalRead(PIR_PIN) == HIGH) {
    unsigned long now = millis();
    if (now - lastPublish >= (unsigned long)COOLDOWN_MS) {
      // Payload expected by Kavach: JSON with "motion" field
      const char* payload = "{\"device\":\"pir_sensor\",\"motion\":\"detected\"}";
      if (mqtt.publish(MQTT_TOPIC_INTRUDER, payload)) {
        Serial.println("Motion published to intruder topic");
        lastPublish = now;
      }
    }
  }

  delay(100);
}
