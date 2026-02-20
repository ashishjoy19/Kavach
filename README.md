# Kavach – Voice + MQTT

**Kavach** is an elderly-focused AI assist device (ESP32-S3 Box-3): **voice recognition**, **minimal UI**, **MQTT** (publish voice commands, subscribe for appliance control), and **LED/light** control.

## What this build includes

- **ESP-SR** – Offline wake word + command recognition.
- **Minimal UI** – One screen: title “Kavach”, status text, and on-screen light (idle / listening / command ok / alert).
- **WiFi** – Simple STA connect using SSID/password from menuconfig (no provisioning UI).
- **MQTT** – Connects to a broker (e.g. Mosquitto on your PC). **Publish only:** help-type commands → `kavach/help`, appliance-type commands → `kavach/appliances`. Your app subscribes and then contacts emergency contacts or controls IoT devices wirelessly.

## MQTT setup

1. **Broker** – Run Mosquitto on your PC (or any MQTT broker on the same network).
2. **Configure** – Set WiFi and broker in menuconfig or in `sdkconfig.defaults`:
   - `idf.py menuconfig` → **Kavach Configuration**:
     - **WiFi SSID** / **WiFi Password** – your network.
     - **MQTT Broker URI** – e.g. `mqtt://192.168.1.100:1883` (use your PC’s IP).
   - Or edit `sdkconfig.defaults`: `CONFIG_KAVACH_WIFI_SSID`, `CONFIG_KAVACH_WIFI_PASSWORD`, `CONFIG_KAVACH_MQTT_BROKER_URI`.
3. **Two topics (publish only)** – The device only **publishes**; it does not subscribe or control any appliance itself. Your app subscribes to these topics and then contacts emergency contacts or controls your IoT devices:

| Topic (default)     | When used | Payload example | Use in your app |
|---------------------|-----------|------------------|------------------|
| **`kavach/help`**   | Help / alert / call family / “Help” | `I need help`, `Send alert`, `Call family`, `Help` | Notify emergency contacts, trigger calls |
| **`kavach/appliances`** | All other voice commands (light, play, pause, AC, etc.) | `Turn on the light`, `Play music`, `Turn off the Air` | Forward to your IoT devices / smart home |

So: **help-related** → one topic for people; **appliance-related** → one topic for devices. No on-device LED or appliance control; everything is published for your app to act on.

## Voice commands

Same as before (Help/Alert, Call family, Help, light on/off, etc.). Each recognised command is published to `kavach/help` or `kavach/appliances` and the UI is updated. Voice confirmation WAVs are disabled by default (`KAVACH_VOICE_CONFIRM 0` in `app_sr_handler.c`).

## Build and flash

```bash
cd examples/kavach_demo
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Set **Board: ESP32-S3-BOX-3** in `idf.py menuconfig` (BSP) if needed.

**If MQTT won’t connect** (e.g. `esp-tls: select() timeout` / `Error transport connect`):

- Use **plain MQTT**: URI must be `mqtt://<IP>:1883` (not `mqtts://`). The project defaults to non-SSL (`CONFIG_MQTT_TRANSPORT_SSL` off in `sdkconfig.defaults`).
- **Broker**: Start Mosquitto on the PC (e.g. `mosquitto -v`). Ensure it’s listening on 0.0.0.0:1883 (or your PC’s IP).
- **Same network**: ESP32 and broker must be on the same LAN; set **MQTT Broker URI** to your PC’s actual IP (e.g. `mqtt://192.168.220.13:1883`).
- **Firewall**: Allow inbound TCP port **1883** on the PC where the broker runs.
- After changing `sdkconfig.defaults` (e.g. disabling SSL), run `idf.py fullclean` then `idf.py build` so the new config is applied.

## Project layout

- `main/main.c` – NVS, settings, **WiFi**, **MQTT**, display, SR start (no LED).
- `main/app/app_wifi_simple.c` – WiFi STA (SSID/password from config).
- `main/app/app_mqtt.c` – MQTT client: **publish only** to `kavach/help` and `kavach/appliances`.
- `main/app/app_sr.c`, `app_sr_handler.c` – SR + handler; handler publishes help commands to help topic and all other commands to appliances topic.
- `main/Kconfig.projbuild` – WiFi SSID/password, MQTT broker URI, and the two topic names.

## References

- [Factory example](../factory_demo/README.md) – full voice + UI reference.
- [ESP-SR](https://github.com/espressif/esp-sr) – wake word and speech commands.
