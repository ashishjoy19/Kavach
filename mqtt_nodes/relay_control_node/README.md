# Relay control node

Subscribes to **`kavach/appliances`** (or **`fabacademy/kavach/appliances`**) and drives a relay for lights or other appliances. Parses:

- **Voice commands:** e.g. "Turn on the light", "Turn off the light" (checks for "on" / "off" in the message).
- **JSON from app:** e.g. `{"device":"light1","state":"ON"}` or `"state":"OFF"`.

## Hardware

- **ESP32** or **ESP8266**.
- **Relay module:** Input pin → `RELAY_PIN` (default GPIO5). Set `RELAY_ON` to `HIGH` or `LOW` to match your module (active-high vs active-low).

## Configuration

In `relay_control_node.ino`:

- `WIFI_SSID`, `WIFI_PASS`, `MQTT_BROKER`: same network and broker as Kavach.
- `MQTT_TOPIC_APPLIANCES`: must match the topic your Kavach (or app) publishes to (`kavach/appliances` or `fabacademy/kavach/appliances`).
- `RELAY_PIN`, `RELAY_ON`: GPIO and logic level for the relay.

## Arduino IDE

1. Install **ESP32** (or ESP8266) board support.
2. Install **PubSubClient** and **ArduinoJson** (Manage Libraries).
3. Open `relay_control_node.ino`, set config, then Upload.

## Extending

- Add more relays and map `device` in JSON to different GPIOs (e.g. `light1` → GPIO5, `fan` → GPIO6).
- Subscribe to a second topic (e.g. `kavach/relay/light1`) for app-only control.
