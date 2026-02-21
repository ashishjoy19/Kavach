# Kavach Flutter app

The **Kavach mobile app** (Flutter) lets users:

- Receive **help/alert** notifications when the Kavach device publishes to `kavach/help` (e.g. "I need help", "Send alert", "Emergency - home button").
- See or relay **appliance commands** from `kavach/appliances` (e.g. "Turn on the light").
- Optionally check if the Kavach device is online by publishing to `fabacademy/kavach/ping` and subscribing to `fabacademy/kavach/pong` (device replies with `pong`).

All communication goes through the **same MQTT broker** as the Kavach device and the other nodes (gas, relay, PIR).

---

## Option A: App in a separate repository

If the Flutter app lives in its own repo, add the link here so the main README can point to it.

**App repository (placeholder):**

```
https://github.com/YOUR_ORG/kavach_flutter_app
```

Replace the URL above with your actual Flutter app repository. Then in the root [README.md](../README.md), the "Flutter app" section already points to this file; you can add one line: *"Source: [app/README.md](app/README.md) → see link to Flutter app repo."*

---

## Option B: App code in this repo

To keep the Flutter app inside this repo:

1. Create a Flutter project under this folder, e.g.:
   ```bash
   cd app
   flutter create kavach_flutter
   ```
2. Add your MQTT client dependency (e.g. `mqtt_client` or `mqtt_client_ext`) and implement:
   - Subscribe to `kavach/help` and `kavach/appliances` (topic names configurable in Kavach).
   - Optional: publish to `fabacademy/kavach/ping`, subscribe to `fabacademy/kavach/pong` for device presence.
3. Document build/run steps in this README (e.g. `flutter pub get`, `flutter run`).

---

## MQTT topics used by the app

| Topic | Direction | Use |
|-------|-----------|-----|
| `kavach/help` (or `fabacademy/kavach/help`) | Subscribe | Help/alert messages; show notification, trigger call to emergency contacts. |
| `kavach/appliances` (or `fabacademy/kavach/appliances`) | Subscribe | Appliance voice commands; show in UI or forward to relay node. |
| `fabacademy/kavach/ping` | Publish | Optional: send "ping"; device replies on `fabacademy/kavach/pong`. |
| `fabacademy/kavach/pong` | Subscribe | Optional: receive "pong" to confirm Kavach device is online. |

Use the same broker host/port and (if required) credentials as configured on the Kavach device.
