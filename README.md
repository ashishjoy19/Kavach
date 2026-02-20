# Kavach – Voice + MQTT Assist Device

**Kavach** is an elderly-focused AI assist device for **ESP32-S3 Box** (and Box Lite): offline **voice recognition**, a **minimal UI**, and **MQTT** integration. The device publishes voice commands to an MQTT broker so your app can notify emergency contacts or control IoT appliances.

---

## Features

| Feature | Description |
|--------|-------------|
| **ESP-SR** | Offline wake word (“Hi ESP” / “Alexa”) and command recognition. |
| **Minimal UI** | Single screen: title “Kavach”, status text, and on-screen state (idle / listening / command ok / alert). Optional clock display. |
| **WiFi** | STA mode using SSID and password from menuconfig or `sdkconfig.defaults` (no provisioning UI). |
| **MQTT** | Publish-only client: help/alert commands → `kavach/help`; appliance commands → `kavach/appliances`. Your backend subscribes and acts (e.g. call family, control lights). |
| **IR learning** | Long-press home button to learn AC remote; voice commands then control AC via IR. |
| **Emergency** | Short-press home button sends an emergency message to the help topic. |

---

## Repository layout

Build and run the **Kavach** application from the `examples/kavach_demo` directory. The repo also contains shared **components** (BSP), **docs**, **hardware**, and **tools**.

```
Kavach_ESP32/
├── README.md                 # This file
├── components/               # Board support (BSP); referenced by the demo
├── docs/                     # Project documentation
├── examples/
│   └── kavach_demo/          # ★ Kavach application (build from here)
│       ├── CMakeLists.txt    # Project and BSP path (../../components)
│       ├── README.md         # Demo-specific readme
│       ├── sdkconfig.defaults
│       ├── main/
│       ├── spiffs/
│       ├── PROJECT_STRUCTURE.md
│       └── VOICE_CONFIRMATIONS.md
├── hardware/
└── tools/
```

---

## Project files (Kavach application)

All paths below are relative to **`examples/kavach_demo/`**.

### Entry and configuration

| Path | Purpose |
|------|--------|
| `main/main.c` | App entry: NVS, settings, WiFi, SNTP, MQTT, display, BSP, UI, IR, speech recognition. Home button: short = emergency, long = IR learn. |
| `main/main.h` | Version or build macros. |
| `main/settings.c`, `main/settings.h` | Persistent settings (e.g. language, volume) in NVS. |
| `main/Kconfig.projbuild` | **Kavach Configuration** in menuconfig: WiFi SSID/password, MQTT broker URI, topic names, timezone, wake word. |
| `main/idf_component.yml` | IDF Component Manager deps: esp-sr, led_strip, qrcode, ir_learn, aht20, at581x. |
| `sdkconfig.defaults` | Default Kconfig (target, SPIRAM, SR models, WiFi/MQTT placeholders). |

### Application modules (`main/app/`)

| Path | Purpose |
|------|--------|
| `main/app/app_wifi_simple.c`, `.h` | WiFi STA using SSID/password from config. |
| `main/app/app_mqtt.c`, `.h` | MQTT client: **publish only** to `kavach/help` and `kavach/appliances` (and optional sensor topic). |
| `main/app/app_sr.c`, `.h` | ESP-SR integration: wake word and command list (Kavach + factory-style). |
| `main/app/app_sr_handler.c`, `.h` | SR result handling: updates UI; publishes help → help topic, others → appliances topic; optional WAV confirmations. |
| `main/app/app_sntp.c`, `.h` | SNTP time sync for UI clock. |
| `main/app/app_ir.c`, `.h` | IR learning and AC control (long-press home). |
| `main/app/sensor_stub.c`, `.h` | Stub for sensor/IR-learn enable. |
| `main/app/mute_stub.c`, `.h` | Stub for mute/play flag. |

### UI (`main/gui/`)

| Path | Purpose |
|------|--------|
| `main/gui/ui_kavach.c`, `ui_kavach.h` | Minimal UI: title “Kavach”, status label, on-screen state; `kavach_ui_set_status()`, `kavach_ui_set_light()`. |
| `main/gui/font/` | LVGL fonts: `font_en_12.c`, `font_en_24.c`, `font_en_64.c`, `font_en_bold_36.c`. |
| `main/gui/image/` | Assets (e.g. `kavach_logo.png`). |

### SPIFFS and assets

| Path | Purpose |
|------|--------|
| `spiffs/` | Voice feedback WAVs (e.g. `beep.wav`, `echo_en_ok.wav`) flashed as the `storage` partition. See `spiffs/README.txt` and `VOICE_CONFIRMATIONS.md`. |

---

## MQTT setup

1. **Broker** – Run Mosquitto (or any MQTT broker) on your PC or server on the same LAN.
2. **Configuration** – Set WiFi and broker in menuconfig or in `examples/kavach_demo/sdkconfig.defaults`:
   - **Menuconfig:** from `examples/kavach_demo`, run `idf.py menuconfig` → **Kavach Configuration**:
     - **WiFi SSID** / **WiFi Password**
     - **MQTT Broker URI** (e.g. `mqtt://192.168.1.100:1883`)
   - **Or edit** `examples/kavach_demo/sdkconfig.defaults`:  
     `CONFIG_KAVACH_WIFI_SSID`, `CONFIG_KAVACH_WIFI_PASSWORD`, `CONFIG_KAVACH_MQTT_BROKER_URI`.
3. **Topics (publish only)** – The device only publishes; your app subscribes and reacts:

| Topic (configurable) | When used | Example payload | Use in your app |
|----------------------|-----------|------------------|------------------|
| **`kavach/help`** | Help / alert / call family / “Help” | `I need help`, `Send alert`, `Emergency - home button` | Notify emergency contacts, trigger calls |
| **`kavach/appliances`** | Other voice commands (light, play, AC, etc.) | `Turn on the light`, `Turn off the Air` | Forward to IoT / smart home |
| **`fabacademy/kavach/sensor`** | Optional temperature/humidity | JSON with sensor data | Monitoring (if sensor enabled) |

---

## Build and flash

From the repository root:

```bash
cd examples/kavach_demo
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

- Set **Board: ESP32-S3-BOX-3** (or Box Lite) in `idf.py menuconfig` (BSP) if needed.
- The demo uses **`../../components`** as `EXTRA_COMPONENT_DIRS`; ensure the repo is cloned with the `components` directory at the root.

**If MQTT does not connect** (e.g. `esp-tls: select() timeout` / `Error transport connect`):

- Use **plain MQTT**: URI must be `mqtt://<IP>:1883` (not `mqtts://`). The project disables SSL by default in `sdkconfig.defaults`.
- Run the broker on the PC (e.g. `mosquitto -v`), listening on `0.0.0.0:1883`.
- Put the ESP32 and broker on the same LAN; set **MQTT Broker URI** to the PC’s IP (e.g. `mqtt://192.168.220.13:1883`).
- Allow inbound TCP port **1883** on the machine running the broker.
- After changing `sdkconfig.defaults`, run `idf.py fullclean` then `idf.py build`.

---

## Voice commands

Wake with “Hi ESP” or “Alexa” (configurable in **Kavach Configuration**). Commands include: Help, Send alert, Call family, Turn on/off the light, Play, Pause, Turn on/off the Air, etc. Each recognised command is published to `kavach/help` or `kavach/appliances` and the UI is updated. Voice confirmation WAVs are optional (see `examples/kavach_demo/VOICE_CONFIRMATIONS.md`).

---

## Further reading

- **`examples/kavach_demo/README.md`** – Demo-specific setup and MQTT details.
- **`examples/kavach_demo/PROJECT_STRUCTURE.md`** – Minimal file set and dependencies (ESP-IDF, BSP, Component Manager).
- **`examples/kavach_demo/VOICE_CONFIRMATIONS.md`** – WAV files and per-command voice feedback.
- **`examples/kavach_demo/spiffs/README.txt`** – SPIFFS WAV format and required files.
- [ESP-SR](https://github.com/espressif/esp-sr) – Wake word and speech recognition.
