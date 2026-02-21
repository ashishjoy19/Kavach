# MQTT node examples for Kavach

Example firmware for nodes that connect to the same MQTT broker as the Kavach voice device. Use these as a starting point for your gas sensor, relay control, and PIR (intruder) sensor nodes.

## Topics reference

| Topic | Direction | Payload / use |
|-------|-----------|----------------|
| `kavach/help` or `fabacademy/kavach/help` | Kavach → broker | Help/alert/call family; Flutter app subscribes. |
| `kavach/appliances` or `fabacademy/kavach/appliances` | Kavach → broker | Appliance commands (light on/off, etc.); relay node subscribes. |
| `fabacademy/kavach/sensor` | Kavach → broker | Temperature/humidity JSON from Kavach device. |
| `fabacademy/kavach/gas` | Gas node → broker | Publish only on leak: `{"device":"gas_sensor","gas":<0-1023>,"state":"LEAK"}`. Kavach subscribes and shows alert. |
| `fabacademy/kavach/intruder` | PIR node → broker | On motion: `{"device":"pir_sensor","motion":"detected"}`. Kavach subscribes and shows alert. |
| `fabacademy/kavach/ping` | App → broker | App publishes; Kavach replies on `fabacademy/kavach/pong` with `pong`. |

## Node folders

| Folder | Description |
|--------|-------------|
| **gas_sensor_node** | Reads analog gas sensor (e.g. MQ-2); publishes to `fabacademy/kavach/gas` when threshold exceeded. |
| **relay_control_node** | Subscribes to `kavach/appliances`; parses "Turn on the light" / JSON and toggles relay GPIO. |
| **pir_sensor_node** | PIR motion sensor; publishes to `fabacademy/kavach/intruder` when motion detected. |

## Requirements

- **Broker:** Same MQTT broker as Kavach (e.g. Mosquitto on your PC or `mqtt.fabcloud.org`).
- **WiFi:** Set SSID/password in each example (or use WiFiManager if you add it).
- **Hardware:** ESP32 or ESP8266; gas sensor (analog), relay module, PIR as per each example.

## Building and flashing

Each node is an **Arduino** or **PlatformIO** project. From the node folder:

- **Arduino IDE:** Open the `.ino` (or `src/main.cpp`), install boards (ESP32/ESP8266) and **PubSubClient**, set board and port, then Upload.
- **PlatformIO:** Run `pio run -t upload` (and `pio run -t monitor` for serial output).

Replace `WIFI_SSID`, `WIFI_PASS`, and `MQTT_BROKER` with your values before building.
