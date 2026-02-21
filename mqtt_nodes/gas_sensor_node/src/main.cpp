/*
 * Kavach gas sensor node - PlatformIO entry (same logic as gas_sensor_node.ino).
 * For Arduino IDE use the .ino file in the parent folder.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define WIFI_SSID     "your_ssid"
#define WIFI_PASS     "your_password"
#define MQTT_BROKER   "192.168.1.100"
#define MQTT_PORT     1883
#define GAS_PIN       34
#define LEAK_THRESHOLD 600
#define POLL_MS       2000
#define MQTT_TOPIC_GAS "fabacademy/kavach/gas"

WiFiClient wifi;
PubSubClient mqtt(wifi);

void setup_wifi() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Gas sensor node starting");
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
    if (mqtt.connect("kavach_gas_sensor")) { Serial.println("connected"); }
    else { Serial.print("failed, rc="); Serial.println(mqtt.state()); delay(3000); }
  }
}

void setup() {
  setup_wifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqtt_callback);
  pinMode(GAS_PIN, INPUT);
}

void loop() {
  if (!mqtt.connected()) mqtt_reconnect();
  mqtt.loop();
  int raw = analogRead(GAS_PIN);
  if (raw >= LEAK_THRESHOLD) {
    char payload[80];
    snprintf(payload, sizeof(payload), "{\"device\":\"gas_sensor\",\"gas\":%d,\"state\":\"LEAK\"}", raw);
    if (mqtt.publish(MQTT_TOPIC_GAS, payload)) Serial.printf("LEAK published: %s\n", payload);
    delay(5000);
  }
  delay(POLL_MS);
}
