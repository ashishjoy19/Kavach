/*
 * Kavach relay control node
 * Subscribes to kavach/appliances (or fabacademy/kavach/appliances). On messages
 * like "Turn on the light" or JSON {"device":"light1","state":"ON"}, toggles relay GPIO.
 *
 * Hardware: ESP32/ESP8266, relay module on RELAY_PIN (e.g. GPIO5).
 * Libraries: WiFi, PubSubClient.
 *
 * Set WIFI_SSID, WIFI_PASS, MQTT_BROKER and RELAY_PIN. Optionally subscribe to
 * fabacademy/kavach/appliances if your Kavach uses that topic.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configure these ---
#define WIFI_SSID       "your_ssid"
#define WIFI_PASS       "your_password"
#define MQTT_BROKER     "192.168.1.100"
#define MQTT_PORT       1883

// Topic: use same as Kavach (kavach/appliances or fabacademy/kavach/appliances)
#define MQTT_TOPIC_APPLIANCES "kavach/appliances"

#define RELAY_PIN       5    // GPIO for relay (LOW = off, HIGH = on, or invert in code)
#define RELAY_ON        HIGH  // Set to LOW if your relay is active-low

WiFiClient wifi;
PubSubClient mqtt(wifi);

void setup_wifi() {
  Serial.begin(115200);
  delay(100);
  Serial.println("Relay control node starting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected, IP: " + WiFi.localIP().toString());
}

void set_relay(bool on) {
  digitalWrite(RELAY_PIN, on ? RELAY_ON : (RELAY_ON == HIGH ? LOW : HIGH));
  Serial.println(on ? "Relay ON" : "Relay OFF");
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  if (length == 0) return;

  // Copy payload as null-terminated string
  char msg[256];
  size_t copy_len = length < sizeof(msg) - 1 ? length : sizeof(msg) - 1;
  memcpy(msg, payload, copy_len);
  msg[copy_len] = '\0';

  Serial.printf("Appliances message: %s\n", msg);

  // Option 1: Plain text from voice (e.g. "Turn on the light", "Turn off the light")
  String s = String(msg);
  s.toLowerCase();
  bool turn_on = (s.indexOf("on") >= 0 && s.indexOf("off") < 0) ||
                 (s.indexOf("turn on") >= 0);

  // Option 2: JSON from app e.g. {"device":"light1","state":"ON"}
  if (strchr(msg, '{') != NULL) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, msg) == DeserializationError::Ok) {
      const char* state = doc["state"];
      if (state != NULL) {
        turn_on = (strcasecmp(state, "ON") == 0 || strcasecmp(state, "1") == 0);
      }
    }
  }

  set_relay(turn_on);
}

void mqtt_reconnect() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting... ");
    if (mqtt.connect("kavach_relay_control")) {
      Serial.println("connected");
      if (mqtt.subscribe(MQTT_TOPIC_APPLIANCES)) {
        Serial.printf("Subscribed to %s\n", MQTT_TOPIC_APPLIANCES);
      }
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
  pinMode(RELAY_PIN, OUTPUT);
  set_relay(false);
}

void loop() {
  if (!mqtt.connected()) {
    mqtt_reconnect();
  }
  mqtt.loop();
  delay(10);
}
