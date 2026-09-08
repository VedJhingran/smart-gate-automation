# ESP32 firmware

`SmartGateAutomation.ino` is generated from the original project by `../scripts/Prepare-PublicPackage.ps1` with secrets moved to `include/Config.h`.

Copy `include/Config.h.example` to `include/Config.h`, populate it locally, and keep it untracked. Install `UniversalTelegramBot`, `Espalexa`, and the ESP32 board support before compiling.

The reference relay is active-low and uses a five-second pulse. Verify the gate-controller interface and relay state using a safe test setup before connecting a live gate.
