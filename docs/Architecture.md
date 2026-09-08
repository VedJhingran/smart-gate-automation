# Architecture

## Components

| Component | Responsibility |
| --- | --- |
| ESP32 | Doorbell sensing, relay timing, local web page, camera relay/cache, Telegram polling, Firebase sync, Alexa discovery |
| Gate controller | Performs physical motor control and retains its manufacturer safety functions |
| Firebase Realtime Database | Synchronizes status, commands, logs, snapshots, and notification tokens |
| PWA | Displays live state and issues authorized requests |
| Telegram | Provides remote visitor notifications and actions |
| Camera/NVR | Supplies local snapshots to the ESP32 |

## Control flow

```text
Doorbell press → ESP32 debounce → visitor state + event → Telegram/Firebase
PWA or Telegram action → Firebase poll/callback → ESP32 relay pulse → status + log
Camera snapshot request → ESP32 cached frame → Telegram upload or Firebase snapshot
```

## Data model

- `status`: current `gateOpen`, `visitorWaiting`, uptime, and update marker.
- `command`: one-shot `OPEN_GATE`, `IGNORE`, or `SNAPSHOT`, cleared by the ESP32 after handling.
- `eventLog`: append-only user-readable records; trim via trusted backend logic.
- `dailyOpens`: count keyed by device-local date. Consider server timestamps for multi-device accuracy.
- `cameraSnapshot`: base64 image and timestamp. Apply size and retention limits.

## Trust boundaries

The ESP32, home network, Firebase rules, Telegram account, and PWA are separate trust boundaries. Internet-facing control should require authenticated, authorized users and should not rely on an embedded static database secret. See [SECURITY.md](../SECURITY.md).
