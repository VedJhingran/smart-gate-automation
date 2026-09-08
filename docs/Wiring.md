# Wiring and safety

## Reference GPIO mapping

| ESP32 pin | Reference role | Notes |
| --- | --- | --- |
| GPIO 23 | Relay trigger | The reference sketch treats the relay as active-low. Verify your module before use. |
| GPIO 27 | Doorbell input | Configured with `INPUT_PULLUP`; an isolated dry contact pulls it to GND. |
| GPIO 2 | Status LED | Optional onboard Wi-Fi indicator. |

## Conceptual wiring

```text
ESP32 GPIO 23 ──> isolated relay input ──> relay dry contact ──> gate controller trigger terminals
ESP32 GPIO 27 <── isolated dry contact <── doorbell sensing circuit
ESP32 GND      ──> interface logic ground only when required by the chosen isolated module
```

The gate-controller trigger terminals, doorbell circuit, and mains wiring vary by installation. Consult the manufacturer documentation and a qualified installer. Do not connect unknown voltage to an ESP32 pin.

## Commissioning checklist

1. Disconnect or disable gate motor actuation.
2. Confirm relay idle/active behavior with a multimeter or indicator.
3. Confirm the contact pulse is momentary and matches the controller requirement.
4. Confirm the doorbell input reads high when idle and low only when pressed.
5. Test auto re-lock timing and network-loss behavior.
6. Restore motor power only after every test is predictable and safety devices are verified.

## Common pitfalls

- Many relay boards are active-low; changing modules without checking can briefly trigger the gate during boot.
- A doorbell may carry AC or a voltage higher than 3.3 V. Use an interface designed for that signal.
- Long unshielded runs can introduce false triggers. Use appropriate cabling, filtering, and enclosure grounding practices.
