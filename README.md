# 🏡 Smart Home Gate Automation System

A cloud-connected smart gate controller built around an ESP32 with integrations for **Firebase**, **Telegram**, **Amazon Alexa**, a **Progressive Web App (PWA)** and a **CP Plus NVR**.

## Features

- Remote gate control from anywhere
- Progressive Web App (Android/iPhone/Desktop)
- Telegram notifications with action buttons
- Alexa voice control (local network)
- Camera snapshots
- Firebase Cloud Messaging push notifications
- Visitor event log
- Daily gate usage statistics
- Automatic relocking

---

## Architecture

![System Architecture](system-architecture.svg)

## Hardware Wiring

![Hardware Wiring](hardware-wiring.svg)

## Software Flow

![Software Flow](software-flow.svg)

## Technology Stack

| Component | Technology |
|-----------|------------|
| Controller | ESP32 |
| Backend | Firebase Realtime Database |
| Mobile App | Progressive Web App |
| Notifications | Firebase Cloud Messaging |
| Messaging | Telegram Bot |
| Voice | Amazon Alexa (Espalexa) |
| Camera | CP Plus NVR |
| Hosting | GitHub Pages / Firebase Hosting |

## Repository

```
/
├── README.md
├── system-architecture.svg
├── hardware-wiring.svg
├── software-flow.svg
├── index.html
├── firebase-messaging-sw.js
├── manifest.json
└── smart_gate_controller.ino
```

## License

MIT
