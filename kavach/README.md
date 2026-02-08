# Kavach – Minimal Voice-Only Build

**Kavach** is an elderly-focused AI assist device (ESP32-S3 Box-3). This folder is a **minimal build**: **voice recognition only** and a **simple UI** (status text + on-screen light). No factory UI, no buttons, no music player, no LED control, no WiFi/provisioning UI. You can add a custom UI, MQTT, appliance control, and temperature/humidity later.

## What this build includes

- **ESP-SR** – Offline wake word + command recognition (unchanged from factory).
- **Minimal UI** – One screen: title “Kavach”, status line, and a coloured circle (light):
  - **Grey** – Idle.
  - **Green** – Wake word detected (“Say command”).
  - **Blue** – Command recognised (shows command text).
  - **Red** – Alert sent (e.g. “I need help” → status “Alert sent”).

No sound feedback, no menus, no buttons. All feedback is on-screen only.

## Voice commands

- **Help / Alert** – “I need help”, “Send alert”, “Emergency” → UI: “Alert sent”, red light, plays `echo_*_alerted.wav` (e.g. “Alerted”).
- **Call family** – “Call family”, “Call my son”, “Call home” → plays `echo_*_calling.wav` (e.g. “Calling”).
- **Help** – “Help”, “What can you do?” → plays `echo_*_help.wav` (e.g. list of commands).
- Other commands → play `echo_*_ok.wav`.

See **VOICE_CONFIRMATIONS.md** for adding custom WAVs (e.g. “Alerted”, “Calling your son”, help summary). For this minimal build, recognised commands only update the status text and light; no hardware (LED, player, AC) is controlled. Help/Alert shows “Alert sent” and red light as a placeholder for future MQTT.

## Build and flash

```bash
cd examples/kavach_demo
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Use **Board: ESP32-S3-BOX-3** in `idf.py menuconfig` (BSP) if needed.

## Project layout (minimal)

- `main/main.c` – NVS, settings, display, SR start; no WiFi, no player, no LED.
- `main/app/app_sr.c`, `app_sr.h` – ESP-SR and Kavach commands (unchanged).
- `main/app/app_sr_handler.c` – Only updates `ui_kavach` (text + light); no echo, no player/LED/sensor.
- `main/app/sensor_stub.c`, `mute_stub.c` – Stubs so SR runs without sensor/mute UI.
- `main/gui/ui_kavach.c`, `ui_kavach.h` – Single screen with title, status label, and light.

Excluded from build: factory UI (ui_main, ui_sr, ui_player, etc.), app_led, app_fan, app_switch, app_sntp, app_wifi, file_manager, rmaker, most fonts and all gui images.

## Adding features later

- **Custom UI** – Replace or extend `ui_kavach.c` and keep using `kavach_ui_set_status()` / `kavach_ui_set_light()` from the SR handler.
- **MQTT** – In `app_sr_handler.c`, in the `SR_CMD_HELP_ALERT` and `SR_CMD_CALL_FAMILY` cases, call your MQTT publish; optionally re-enable `app_wifi` and provisioning.
- **Appliance control** – Re-add `app_led` (or your relay driver) and handle light/plug commands in the handler.
- **Temperature/humidity** – Add sensor driver and publish or threshold-check in a task; trigger alert via MQTT when needed.

## References

- [Factory example](../factory_demo/README.md) – full voice + UI reference.
- [ESP-SR](https://github.com/espressif/esp-sr) – wake word and speech commands.
