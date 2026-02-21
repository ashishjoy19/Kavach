/*
 * Kavach PIR (intruder) sensor node - PlatformIO entry (same logic as pir_sensor_node.ino).
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID     "your_ssid"
#define WIFI_PASS     "your_password"
#define MQTT_BROKER   "192.168.1.100"
#define MQTT_PORT     1883
#define PIR_PIN       4
#define MQTT_TOPIC_INTRUDER "fabacademy/kavach/intruder"
#define COOLDOWN_MS   5000

WiFiClient wifi;
PubSubClient mqtt(wifi);
unsigned long lastPublish = 0;

void setup_wifi() {
  Serial.begin(115200);
  delay(100);
  Serial.println("PIR sensor node starting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) { (void)topic; (void)payload; (void)length; }

void mqtt_reconnect() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting... ");
    if (mqtt.connect("kavach_pir_sensor")) { Serial.println("connected"); }
    else { Serial.print("failed, rc="); Serial.println(mqtt.state()); delay(3000); }
  }
}

void setup() {
  setup_wifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
  pinMode(PIR_PIN, INPUT);
}

void loop() {
  if (!mqtt.connected()) mqtt_reconnect();
  mqtt.loop();
  if (digitalRead(PIR_PIN) == HIGH) {
    unsigned long now = millis();
    if (now - lastPublish >= (unsigned long)COOLDOWN_MS) {
      if (mqtt.publish(MQTT_TOPIC_INTRUDER, "{\"device\":\"pir_sensor\",\"motion\":\"detected\"}")) {
        Serial.println("Motion published to intruder topic");
        lastPublish = now;
      }
    }
  }
  delay(100);
}
