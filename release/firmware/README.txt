CH570Q firmware images built from the tagged source revision.

CH570Q-Dongle.hex: flash through the Dongle USB ISP procedure.
CH570Q-Probe.hex: flash through the Probe UART/ISP procedure.

The firmware pair must be upgraded together because the RF telemetry packet
and BLE broadcast payload are versioned as a pair.
