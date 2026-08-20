# Telemetry Measurement Notes

## Real-time values

`ESP.getCpuFreqMHz()`, `ESP.getFreeHeap()`, `ESP.getMinFreeHeap()`, `ESP.getFlashChipSize()`, `ESP.getSketchSize()`, `ESP.getFreeSketchSpace()`, `WiFi.status()` and `WiFi.RSSI()` are reported only when compiled under ESP32 support and exposed by the selected Arduino-ESP32 core.

## Deliberately not fabricated

This release does not claim generic automatic measurements for arbitrary UART throughput, I2C throughput, SPI throughput, Bluetooth throughput, sensor sampling rate, GPIO inventory, ADC voltage, DAC voltage, PWM output, NVS usage or internal temperature across every ESP32 family. Those require target/API-specific instrumentation and, in some cases, external hardware configuration.

A future implementation can add per-peripheral measurement adapters while preserving the explicit telemetry-state model.

## Benchmark interpretation

`benchmarkLoop()` reports a repeated microsecond average for a fixed software workload. It is a repeatable software timing probe, not a laboratory CPU benchmark and not a claimed cross-board performance rating.
