# Telemetry Model — AmelTechBot

## Core principle

**Every measurable field carries an explicit status. Zero never means
"unsupported."** Statuses:

| Status | Meaning |
|---|---|
| `LIVE` | Measured directly on this call |
| `CACHED` | Documented/reference data (e.g. known GPIO counts per chip family), not a live probe |
| `STALE` | Reserved for future use (e.g. a value older than a defined freshness window) |
| `UNAVAILABLE` | Supported in principle but not measured this call (e.g. requires full scan, or requires the user's sketch to have configured a peripheral first) |
| `UNSUPPORTED` | Not supported by this chip/family/build at all |
| `MEASUREMENT_ERROR` | An attempt was made but the underlying API call failed |

Every scalar is wrapped in `Measurement<T>` with `.value`, `.status`, and
`.timestampMs`.

## Two-speed telemetry

- **Fast** (`getTelemetry(false)`): chip info, CPU frequency, loop timing (if
  `recordLoopSample()` has been called), free heap, Wi-Fi connection/RSSI,
  Bluetooth support flag, UART baud rate, temperature, system uptime/reset
  reason. Safe to call on every `ask()`.
- **Full** (`getTelemetry(true)`): adds largest free heap block, heap
  fragmentation estimate, PSRAM, flash size, firmware size, sketch free
  space, I2C device scan, and documented GPIO capability counts. More
  expensive — call explicitly, not on a hot path.

## Field-by-field notes

### CPU
- `frequencyMHz`: from `getCpuFrequencyMhz()`.
- `lastLoopTimeUs` / `minLoopTimeUs` / `avgLoopTimeUs` / `maxLoopTimeUs` /
  `jitterUs`: **only populated if the sketch calls
  `Telemetry::recordLoopSample(durationUs)` once per `loop()` iteration.**
  Otherwise `UNAVAILABLE` — never fabricated.
- `benchmarkScore`: reserved for an explicit busy-loop benchmark; currently
  always `UNAVAILABLE` in this release (not implicitly run to avoid
  blocking `loop()` unexpectedly).

### Memory
- `freeHeapBytes` / `minFreeHeapBytes`: `ESP.getFreeHeap()` /
  `ESP.getMinFreeHeap()`, fast.
- `largestFreeBlockBytes`, `heapFragmentationPct`, `psramTotalBytes`,
  `psramFreeBytes`, `flashSizeBytes`, `firmwareSizeBytes`,
  `sketchFreeSpaceBytes`: full-scan only.
- Fragmentation is computed as `100 * (1 - largestBlock/freeHeap)` — a
  simple estimate, not a guarantee of allocator behavior.

### Wi-Fi
- `connected`, `rssiDbm`, `signalQuality`: fast, from `WiFi.status()` /
  `WiFi.RSSI()`.
- `configuredLinkRateMbps` (PHY rate) and `measuredTxThroughputKbps` /
  `measuredRxThroughputKbps` are **kept explicitly distinct** per design
  requirements. Neither is populated by default sampling — the library
  does not report PHY rate as throughput, and does not run a bandwidth
  benchmark implicitly (it would consume the user's network and time
  budget without consent). Both remain `UNAVAILABLE` unless a future
  explicit benchmark API is called.

### Bluetooth
- `supported`: compile-time capability check (`CONFIG_BT_ENABLED`).
- `connected`, throughput, packet rate: `UNAVAILABLE`/`UNSUPPORTED` — the
  library does not initialize a BT stack on the user's behalf.

### UART
- `configuredBaudRate`: `Serial.baudRate()`, only if `Serial.begin()` was
  already called by the sketch.
- Throughput/error counters: `UNAVAILABLE` (require an explicit benchmark
  with a real peer device).

### I2C
- `detectedDeviceCount` + `detectedAddresses[]`: a real bus scan
  (`Wire.beginTransmission`/`endTransmission`), full-scan only. **A
  responding address does not identify which sensor is present** — no
  device-identification logic is implemented.
- `configuredClockHz`, `transferBenchmarkKbps`, `errorCount`: `UNAVAILABLE`
  (core APIs don't universally expose configured clock; benchmarking
  requires a real peer).

### SPI
- All fields `UNAVAILABLE` unless the sketch has configured SPI itself;
  the library does not assume a configuration.

### GPIO
- `usablePinCount` / `restrictedPinCount`: **documented reference values
  per chip family** (status `CACHED`, not `LIVE`), conservative estimates.
  Actual usable pins can be lower depending on the specific board/module —
  see your board's datasheet.

### ADC / DAC / PWM
- Not included in general telemetry snapshots because a specific pin/
  channel argument is required to be meaningful. `UNAVAILABLE` in
  snapshots; use dedicated pin-specific calls (application-level, outside
  the general snapshot) if needed.
- `dac.supported`: `true` only for classic ESP32 (2 onboard DAC channels);
  `false`/`UNSUPPORTED` on families without a DAC (S2 has DAC, S3/C3/C6/H2
  generally do not — verify against your exact chip).

### Temperature
- `internalTempC`: only populated if `driver/temp_sensor.h` is available
  at compile time and the read succeeds. Many classic ESP32 Arduino-core
  configurations do not expose a reliable public API for this — in that
  case, reported as `UNSUPPORTED`, never guessed.

### Watchdog / System
- `watchdog.*`: `UNAVAILABLE` (no universal cross-core-version getter).
- `system.uptimeMs`: `millis()`.
- `system.resetReason`: `esp_reset_reason()`, decoded to a string.
- `system.rebootCount`: `UNAVAILABLE` — requires the sketch to opt into
  persistent reboot counting (not done automatically, to avoid
  unsolicited flash wear).

## Health Scoring

See `Diagnostics.h`/`.cpp`. Each subsystem (CPU, Memory, Wi-Fi,
Communication, System) is scored 0–100 **only if it has at least one
`LIVE` measurement**; unmeasured subsystems are marked `UNKNOWN` and
excluded from the overall average — never penalized or fabricated. The
overall score is the average of measured subsystems, and `mainIssue`
reports the worst subsystem at `WARNING` level or worse.
