# PIR (intruder) sensor node

Publishes to **`fabacademy/kavach/intruder`** when the PIR sensor detects motion. The Kavach device subscribes to this topic and shows an intruder/motion alert on screen.

## Payload format

Publish JSON that contains a `"motion"` field (e.g. for the Kavach parser). Example:

```json
{"device":"pir_sensor","motion":"detected"}
```

## Hardware

- **ESP32** or **ESP8266**.
- **PIR sensor** (e.g. HC-SR501): VCC, GND, OUT → `PIR_PIN` (default GPIO4). OUT goes HIGH when motion is detected and may stay HIGH for a few seconds (sensor-dependent); `COOLDOWN_MS` in the sketch limits how often we publish.

## Configuration

In `pir_sensor_node.ino`:

- `WIFI_SSID`, `WIFI_PASS`, `MQTT_BROKER`: same as Kavach.
- `PIR_PIN`: GPIO connected to PIR output.
- `COOLDOWN_MS`: minimum interval between two published alerts (e.g. 5000 = 5 seconds).

## Arduino IDE

1. Install **ESP32** (or ESP8266) board support.
2. Install **PubSubClient**.
3. Open `pir_sensor_node.ino`, set config, then Upload and Serial Monitor (115200).

## PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = knolleary/PubSubClient@^2.8
```

Then: `pio run -t upload` and `pio device monitor -b 115200`.
