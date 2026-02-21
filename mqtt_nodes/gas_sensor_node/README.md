# Gas sensor node

Publishes to **`fabacademy/kavach/gas`** when the gas sensor reading exceeds a threshold. The Kavach device subscribes to this topic and shows a full-screen gas leak alert and plays `gas_alarm.wav`.

## Payload format

Publish JSON that contains the string `"LEAK"` (e.g. for the Kavach parser). Example:

```json
{"device":"gas_sensor","gas":99,"state":"LEAK"}
```

- `gas`: raw ADC value (0–1023) or percentage; optional for display.
- `state`: must be `"LEAK"` to trigger the alert on Kavach.

## Hardware

- **ESP32** (or ESP8266 with ADC).
- **Gas sensor** (e.g. MQ-2): VCC, GND, analog out → `GAS_PIN` (default GPIO34 on ESP32).
- Tune `LEAK_THRESHOLD` in the sketch for your sensor and environment.

## Arduino IDE

1. Install **ESP32** (or ESP8266) board support.
2. Install **PubSubClient** (Sketch → Include Library → Manage Libraries → search "PubSubClient").
3. Open `gas_sensor_node.ino`, set `WIFI_SSID`, `WIFI_PASS`, `MQTT_BROKER`, and optionally `GAS_PIN` / `LEAK_THRESHOLD`.
4. Upload and open Serial Monitor (115200).

## PlatformIO

Create `platformio.ini` in this folder if needed:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = knolleary/PubSubClient@^2.8
```

Then: `pio run -t upload` and `pio device monitor -b 115200`.
