# Hardware

## Core controller

The reference build uses an ESP32 Dev Module. GPIO 23 drives the gate relay, GPIO 27 receives the isolated doorbell state, and GPIO 2 indicates Wi-Fi state. Pin assignments are configuration assumptions, not universal wiring instructions.

## Required interfaces

| Function | Recommended interface | Why |
| --- | --- | --- |
| Gate trigger | Opto-isolated relay or dry-contact interface | Separates ESP32 logic from gate-controller circuits |
| Doorbell sense | Opto-isolated input or relay dry contact | Prevents external doorbell voltage reaching GPIO |
| Power | Regulated supply with adequate current margin | Avoids brownouts during Wi-Fi activity |
| Camera (optional) | Local NVR/IP camera snapshot endpoint | Supplies images to Telegram and the PWA |

## Electrical and physical notes

- Confirm whether the gate controller expects a momentary dry-contact pulse, not a powered output.
- Share grounds only where the interface design explicitly requires it. Isolation is preferred at the boundary.
- House mains and low-voltage circuits separately and follow local electrical regulations.
- Keep a manual release and original safety devices operational. This software is not a replacement for certified gate safety hardware.
- Install the ESP32 in a protected enclosure with strain relief and a stable antenna/Wi-Fi path.

## Suggested bill of materials

- 1 × ESP32 development board
- 1 × opto-isolated relay module or dry-contact relay interface
- 1 × isolated doorbell sensing module / relay contact
- 1 × appropriately rated regulated power supply
- Enclosure, terminals, fuses, and wiring suitable for the installation
- Optional compatible IP camera or NVR

Read [Wiring](Wiring.md) before connecting anything to the gate controller.
