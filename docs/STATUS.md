# Health scoring

```cpp
Serial.println(bot.getHealthReport());
Serial.println(bot.getHealthScore());     // 0-100
Serial.println(bot.runDiagnostics(true)); // full dump with actions
```

```
ESP32 Health: 91/100 [NORMAL]  confidence 84%
Memory: NORMAL (94) - 214 KB free of 320 KB
Stability: NORMAL (96) - reset POWERON, up 412s, 0 errors
Thermal: NORMAL (97) - 48.2 C (die sensor)
Wi-Fi: INFO (80) - not connected
CPU: NORMAL (96) - 240 MHz
Storage: NORMAL (96) - 1204 KB sketch, 1892 KB free
Sensors: NORMAL (96) - DHT22, 214 ok / 3 failed (1%)
Main issue: No significant issues detected.
```

---

## What changed in 2.0.0, and why it is more accurate

Version 1.1.0 computed the health score as the **unweighted mean of five
components**, where an unmeasurable component scored a flat 50. That produced
two systematic errors:

1. **A board with no die temperature sensor looked half broken.** The classic
   ESP32 cannot report its die temperature, so Thermal scored 50 and dragged
   the overall score down by ten points — permanently, for a board that was
   perfectly healthy.
2. **A critically low heap and an idle Wi-Fi radio counted equally.** They are
   not remotely equal. One is about to crash the device; the other is a
   deliberate configuration choice.

Version 2 fixes both:

- Components are **weighted** by how much they actually matter.
- Each component reports a **confidence**, and the overall score is the
  confidence-weighted mean over components that were genuinely measured. What
  cannot be measured is excluded, and the report's own confidence drops
  instead of its score.
- Scores follow a **continuous curve**, so one byte of heap cannot swing the
  result by 25 points.
- Every component carries a **recommended action**.

---

## Components and weights

| Component | Weight | What it looks at |
|---|---|---|
| Memory | 26 | free heap, fragmentation, low-water mark |
| Stability | 20 | reset reason, error counters, loop latency, stack headroom |
| Thermal | 18 | die temperature, or ambient estimate |
| Wi-Fi | 12 | connection state, RSSI, disconnect count |
| CPU | 8 | clock frequency, throttling, duty cycle |
| Storage | 8 | spare app partition, OTA headroom |
| Sensors | 8 | DHT success rate, consecutive failures, freshness |

Weights sum to 100. Memory carries the most because heap exhaustion is the most
common way an ESP32 sketch actually dies.

---

## How the score is computed

```
overall     = sum(score * weight * confidence) / sum(weight * confidence)
confidence  = sum(weight * confidence) / sum(weight)
```

Only components with non-zero confidence enter the first sum. A component that
could not be measured contributes nothing to the score and reduces the reported
confidence.

So a classic ESP32 with no sensors reports something like:

```
ESP32 Health: 93/100 [NORMAL]  confidence 70%
Thermal: not measured - no readable temperature source
Sensors: not measured - no DHT sensor configured
```

93 is an honest score for the seven-tenths of the picture that is visible,
rather than a pessimistic 71 built partly from guesses.

### Levels

| Level | Score |
|---|---|
| `HEALTH_NORMAL` | 88–100 |
| `HEALTH_INFO` | 75–87 |
| `HEALTH_WARNING` | 58–74 |
| `HEALTH_HIGH` | 38–57 |
| `HEALTH_CRITICAL` | 0–37 |

One critical component is never hidden behind a good average. If the worst
measured component is more severe than the overall level implies, the overall
level is raised to match it.

---

## Confidence per component

| Confidence | Meaning |
|---|---|
| 90–95 | direct hardware reading |
| 85 | reliable derived value |
| 55 | estimate, such as die temperature inferred from ambient |
| 40–50 | partial information |
| 30 | configured but not yet exercised |
| 0 | not measured — excluded from the score |

A die temperature estimated from a DHT sensor plus a 15 °C offset is genuinely
useful, but it should not carry the same authority as a real sensor. Confidence
55 lets it inform the score without dominating it.

---

## What each component checks

### Memory (26)

Free heap on a curve from 8 KB to 160 KB, blended with the free ratio.
Penalties: fragmentation above 35 / 50 / 70 %, and a low-water mark under
16 KB or 8 KB. Actions range from "none needed" through "training is blocked
below the reserved minimum" to "reduce String use, shrink buffers, clear
history".

### Stability (20)

Reset reason is the strongest signal. A brownout scores 34 and says to check
the power supply; a panic or watchdog reset scores 38–42 and points at the
backtrace. Library error counts subtract up to 30. Loop latency above 500 ms
and task stack headroom below 512 bytes each subtract more.

### Thermal (18)

Banded on temperature: 97 below 55 °C, 86 to 70 °C, 66 to 80 °C, 42 to 90 °C,
18 above. Confidence 90 for a real die sensor, 55 for an ambient estimate, 0
when neither exists.

### Wi-Fi (12)

Not connected is **not** a fault — it scores 80 at confidence 40, because this
library is designed to work offline. When connected, RSSI is scored on a curve
from −50 dBm (excellent) to −88 dBm (unusable), with a 15-point penalty for
three or more disconnects.

### CPU (8)

96 at 160 MHz or above, 88 at 80 MHz, 70 below that (Wi-Fi becomes unreliable),
68 while thermally throttled. Capped at 72 if the duty cycle exceeds 80 %.

### Storage (8)

96 when free sketch space exceeds the sketch size, meaning OTA has room. 80
when there is over 64 KB but not enough for an OTA image. 55 below that, with
advice to pick a larger app partition.

### Sensors (8)

Confidence 0 when no DHT is configured — an absent optional sensor is not a
fault. Otherwise: 96 for a healthy sensor, 70 above a 15 % error rate, 45 above
40 %, and 22 for three or more consecutive failures, with wiring advice.

---

## Trend

The report includes `trendDelta`, the change against an exponential moving
average of previous scores.

```
ESP32 Health: 78/100 [INFO]  confidence 84%
Trend: worsening
```

A slow heap leak shows up here long before the absolute score crosses a
threshold.

---

## Reading it programmatically

```cpp
const HealthReport& hr = bot.diagnostics().evaluateHealth();

Serial.println(hr.overallScore);
Serial.println(hr.overallConfidence);
Serial.println(Diagnostics::levelToString(hr.overallLevel));
Serial.println(hr.mainIssue);

for (uint8_t i = 0; i < hr.componentCount; ++i) {
    const HealthComponent& c = hr.components[i];
    if (c.confidence == 0) continue;          // not measured
    Serial.printf("%s: %s (%u) - %s\n  action: %s\n",
                  c.name,
                  Diagnostics::levelToString(c.level),
                  c.score, c.detail, c.action);
}
```

`evaluateHealth()` returns a **const reference**, not a copy. The report is
about 1.5 KB and copying it onto a task stack on every call would be wasteful.
It stays valid until the next call.

---

## Full diagnostics

`runDiagnostics(true)` refreshes everything, prints the raw telemetry, the
thermal report, the health breakdown, and then a list of actions for every
component at `WARNING` or worse:

```
Recommended actions:
- Memory: Heap is tight. Training is blocked below the reserved minimum.
- Thermal: Running warm. Automatic throttling engages at 80 C.
```

When nothing is wrong it says so, rather than padding the output:

```
Recommended actions:
- None. Everything measurable is within normal limits.
```

---

See also: [TELEMETRY.md](TELEMETRY.md), [SENSORS.md](SENSORS.md), [API.md](API.md).
