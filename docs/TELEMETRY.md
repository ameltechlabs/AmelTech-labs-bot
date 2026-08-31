# Telemetry

What the library can measure about the board it is running on, and — just as
importantly — what it cannot.

---

## The core rule

Every measurement carries a `MeasurementStatus`. A value you cannot trust is
never presented as one you can.

| Status | Meaning |
|---|---|
| `MEAS_LIVE` | read successfully, just now |
| `MEAS_CACHED` | a previous good value, still within its useful window |
| `MEAS_STALE` | too old to rely on, kept only for reference |
| `MEAS_UNAVAILABLE` | the read was attempted and failed |
| `MEAS_UNSUPPORTED` | this chip or core cannot provide it at all |
| `MEAS_ERROR` | the read returned something impossible |

```cpp
const ESP32Telemetry& t = bot.getTelemetry(true);

if (Telemetry::statusIsUsable(t.freeHeap.status)) {
    Serial.println(t.freeHeap.value);
} else {
    Serial.println(Telemetry::statusToString(t.freeHeap.status));
}
```

`statusIsUsable()` is true for `MEAS_LIVE` and `MEAS_CACHED` only.

---

## Available fields

### Memory

| Field | Notes |
|---|---|
| `freeHeap` | bytes currently free |
| `heapSize` | total heap |
| `minFreeHeap` | low-water mark since boot — reveals pressure a snapshot misses |
| `maxAllocHeap` | largest block that can still be allocated |
| `heapFragmentationPct` | derived: `100 - (maxAlloc * 100 / free)` |
| `psramSize`, `freePsram` | `MEAS_UNSUPPORTED` when no PSRAM is fitted |

Fragmentation matters independently of free heap. A heap that is 60 % free but
70 % fragmented cannot satisfy a 20 KB allocation, and the Memory health
component penalises it accordingly.

### CPU and chip

| Field | Notes |
|---|---|
| `cpuFreqMhz` | current clock, which the thermal guard may have lowered |
| `chipModel` | from `ESP.getChipModel()` where available |
| `chipCores`, `chipRevision` | |
| `sdkVersion` | ESP-IDF version string |
| `taskStackHighWater` | bytes of loop-task stack never used |

### Storage

| Field | Notes |
|---|---|
| `flashSize` | total flash |
| `sketchSize` | compiled size of this sketch |
| `freeSketchSpace` | spare app partition, which is what OTA needs |

### Network

| Field | Notes |
|---|---|
| `wifiConnected` | `MEAS_UNSUPPORTED` in builds without Wi-Fi |
| `wifiRssi` | dBm; only meaningful while connected |
| `wifiSsid` | with its own `wifiSsidStatus` |
| `wifiIp`, `wifiMac` | |
| `wifiDisconnectCount` | counted since boot |

Wi-Fi is polled at most once per full update. Version 1.1.0 called
`WiFi.status()` and `WiFi.RSSI()` on every single telemetry read, which is a
surprisingly expensive pair of calls to make in a chat loop.

### Temperature

| Field | Notes |
|---|---|
| `temperatureC` | die temperature — frequently `MEAS_UNSUPPORTED`, see below |
| `ambientTemperatureC` | from a DHT sensor, when one is attached |
| `ambientHumidity` | |

### System

| Field | Notes |
|---|---|
| `uptimeMs` | with rollover handling |
| `resetReason` | why the board last restarted |
| `errorCount`, `warningCount` | counted by the library itself |
| `loopLatencyMs` | exponential moving average of the gap between `tick()` calls |

---

## The 53.33 °C problem

The classic ESP32 (the original, not the S2, S3, C3 or C6) has no usable
internal temperature sensor. Its `temperatureRead()` function exists and
returns a number, but that number is a **constant 53.33 °C placeholder**. It
has nothing to do with the die temperature.

Version 1.1.0 reported it as a live measurement. Dashboards built on that
showed a perfectly flat, entirely fictional temperature line.

Version 2 detects the placeholder value on the first read and marks the field
`MEAS_UNSUPPORTED` from then on:

```
> what is the cpu temperature
This chip has no usable internal temperature sensor, so I cannot give you a
real die temperature. Ambient is 24.3 C, and the die usually runs 10-20 C
above that.
```

The S2, S3, C3 and C6 have real sensors and are read normally.

---

## Update rates

Telemetry is split into fast and full updates so a chat loop does not spend its
time querying hardware.

| Update | Default interval | Reads |
|---|---|---|
| `updateFast()` | 1000 ms | heap, uptime, CPU frequency |
| `updateFull()` | 5000 ms | everything, including Wi-Fi and temperature |

`bot.tick()` calls `updateFast()`; the diagnostics path calls `updateFull()`.
Both accept `force = true` to bypass the rate limit.

Fields that were not refreshed on this pass are **aged**: a `MEAS_LIVE` value
becomes `MEAS_CACHED`, and eventually `MEAS_STALE`. So a status of `LIVE`
genuinely means "read on this pass", not "read at some point".

Intervals are configurable in `AmelTechConfig.h`:

```c
#define AMELTECH_TELEMETRY_FAST_MS 1000
#define AMELTECH_TELEMETRY_FULL_MS 5000
```

---

## Thermal protection

`ThermalGuard` sits on top of telemetry and acts on what it finds.

| State | Threshold | Action |
|---|---|---|
| `THERMAL_NORMAL` | below 70 °C | none |
| `THERMAL_WARM` | 70 °C | longer cooperative yields |
| `THERMAL_HOT` | 80 °C | CPU dropped to 80 MHz |
| `THERMAL_CRITICAL` | 90 °C | maximum throttling, warning logged |

There is 4 °C of hysteresis on the way back down, so a board sitting on a
threshold does not oscillate between states and clock speeds.

```cpp
Serial.println(bot.getThermalReport());
Serial.println(bot.thermal().dutyPercent());
```

`dutyPercent()` is the share of a rolling window spent inside `ask()`. A
sustained value above 80 % means the loop never idles, which generates heat and
risks a watchdog reset — the CPU health component flags it.

When no temperature source exists, the guard reports `THERMAL_UNKNOWN` and
keeps its cooperative yielding active. It limits workload on principle rather
than pretending to know a temperature it cannot read.

---

## Counters

The library reports on itself as well as on the hardware:

```cpp
bot.telemetry().noteError();
bot.telemetry().noteWarning();
bot.telemetry().noteWifiDisconnect();
bot.telemetry().resetCounters();
```

`errorCount` feeds the Stability health component, so a library that keeps
hitting problems shows up in the score rather than failing quietly.

---

## Reading it all

```cpp
const ESP32Telemetry& t = bot.getTelemetry(true);

Serial.printf("Heap %u/%u KB (%u%% frag)\n",
              t.freeHeap.value / 1024,
              t.heapSize.value / 1024,
              t.heapFragmentationPct.value);

if (Telemetry::statusIsUsable(t.temperatureC.status)) {
    Serial.printf("Die %.1f C\n", t.temperatureC.value);
} else {
    Serial.printf("Die temperature: %s\n",
                  Telemetry::statusToString(t.temperatureC.status));
}
```

Or let the library format it:

```cpp
Serial.println(bot.runDiagnostics(true));
```

---

See also: [STATUS.md](STATUS.md), [SENSORS.md](SENSORS.md), [API.md](API.md).
