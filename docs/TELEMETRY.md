# Telemetry Model

Every measurable field carries an explicit **status**. Zero is never used to mean “unsupported”.

## Status values

| Status | Meaning |
|--------|---------|
| LIVE | Fresh measurement |
| CACHED | Recently measured, still considered valid |
| STALE | Older than policy window |
| UNAVAILABLE | Hardware present but value not obtainable (e.g. Wi-Fi not connected) |
| UNSUPPORTED | Peripheral / API not available on this chip or build |
| MEASUREMENT_ERROR | API called but returned unusable data |

## Fields (summary)

- **Chip**: model, cores, revision, MAC
- **CPU**: frequency (MHz)
- **Memory**: free heap, min free heap, heap size, largest free block, PSRAM, flash, sketch size
- **Wi-Fi**: connected, RSSI, SSID, status; TX/RX **throughput** default to UNAVAILABLE (not claimed as link rate)
- **Temperature**: internal sensor when API exists; otherwise UNSUPPORTED
- **System**: uptime, reset reason

## Two-speed updates

- **Fast**: CPU freq, free heap, uptime, Wi-Fi RSSI/status
- **Full**: chip info, flash, PSRAM, temperature, etc.

Call `getTelemetry(false)` for fast path, `getTelemetry(true)` or `runDiagnostics(true)` for full scan.

## Rules

- Never fabricate RSSI, temperature, throughput, or GPIO states
- Do not report configured PHY rate as measured application throughput
- Family-specific peripherals (DAC, BT, Hall, internal temp) may be UNSUPPORTED
