# AmelTech lab's bot

**Version 1.0.0**

AmelTech lab's bot is a lightweight, deterministic, offline-capable hybrid chatbot and ESP32 hardware diagnostic library for Arduino-ESP32. It combines bounded knowledge retrieval, spelling-tolerant matching, safe arithmetic, intent routing, confidence scoring, recent context storage, and evidence-based ESP32 telemetry.

> Hardware data is never synthesized. A field is reported as `UNSUPPORTED`, `UNAVAILABLE`, or `MEASUREMENT_ERROR` when the selected ESP32 target or API cannot provide a reliable value.

## Supported targets

The design is family-aware for ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6 and ESP32-H2 where the installed Arduino-ESP32 core exposes the needed APIs. Exact peripheral availability depends on the chip, board, core version and board configuration.

## Installation

Copy this directory into your Arduino `libraries` folder, or import the ZIP from Arduino IDE. The library depends only on the Arduino core; ESP32-specific measurements are conditionally compiled behind `ESP32` support.

## Quick start

```cpp
#include <AmelTechBot.h>

AmelTechBot bot(&Serial);

void setup() {
  Serial.begin(115200);
  bot.begin();
  bot.train("what is an apple", "An apple is a fruit.", "general");
  bot.train("how many seconds are there in one minute", "There are 60 seconds in one minute.", "general");

  bot.ask("how many sec r there in 1 min");
  bot.ask("25 * 4");
  bot.ask("How much RAM do I have?");
}

void loop() {}
```

## Knowledge base

```cpp
bot.addQA("what is water", "Water is a chemical compound made of hydrogen and oxygen.", "science");
bot.train("what is wifi", "Wi-Fi is a wireless networking technology.", "general");
size_t n = bot.getKnowledgeCount();
bot.saveKnowledge();
bot.loadKnowledge();
```

Entries are bounded (`48` by default), validated for emptiness and length, and rejected when they duplicate or strongly conflict with an existing question. NVS persistence is enabled only when ESP32 `Preferences` support is available.

## Calculator

The calculator supports `+`, `-`, `*`, `/`, decimals and parentheses with operator precedence. Division by zero and malformed expressions fail safely. It does not execute arbitrary code.

Examples: `25 * 4`, `(12 + 8) / 2`, `10.5 * 3`.

## Hardware diagnostics

Fast diagnostics are intended for lightweight status checks:

- CPU frequency reported by the ESP32 core.
- Free heap and minimum free heap.
- Uptime.
- Wi-Fi connection state and RSSI.

Slow/full diagnostics can additionally inspect reported flash usage and reset reason and identify the ESP32 target family. Detailed I2C, SPI, UART, Bluetooth, GPIO, ADC, DAC, PWM and sensor instrumentation is intentionally not fabricated: unsupported fields remain explicitly unavailable until a reliable target-specific measurement is implemented.

## Telemetry states

Every measurement has a state:

- `LIVE`: measured from the target during the current read.
- `CACHED`: previously measured and still considered usable.
- `STALE`: cached but outside the freshness policy.
- `UNAVAILABLE`: the API/platform cannot provide a current value.
- `UNSUPPORTED`: the capability is not supported by the selected target/API.
- `MEASUREMENT_ERROR`: measurement was attempted but failed.

Zero is not used as a universal unsupported sentinel.

## Confidence

Responses expose separate confidence fields for answer quality, measurement quality, data freshness and knowledge quality. The default policy is:

- `>= 0.90`: strong answer
- `0.75–0.89`: moderate answer
- `0.50–0.74`: clarification preferred
- `< 0.50`: unknown/insufficient evidence

A hardware reading also reports its telemetry state.

## Health engine

`HealthEngine` evaluates only evidence actually available. It never creates values for missing telemetry. The first release applies documented, conservative rules to free heap and Wi-Fi RSSI and returns `UNKNOWN` when no live evidence exists.

## Context handling

The public class retains only a bounded recent question/answer pair in RAM in this release. This keeps context inexpensive and prevents unbounded conversation history growth. The API is structured so a larger bounded context store can be added without changing the user-facing routing model.

## Trolling mode

```cpp
bot.enableTrolling(true);
```

Humour is optional and secondary to the technical answer. It is never used to replace warnings or critical diagnostic information.

## API reference

### `AmelTechBot`

- `begin()`
- `ask(question)`
- `train(question, answer, category)`
- `addQA(...)`
- `removeQA(...)`
- `clearKnowledge()`
- `getKnowledgeCount()`
- `saveKnowledge()` / `loadKnowledge()`
- `telemetry()`
- `diagnostics().refreshFast()`
- `diagnostics().refreshSlow()`
- `diagnostics().fullScan()`
- `enableTrolling(bool)`
- `lastError()`

### `KnowledgeBase`

Adds bounded, validated Q&A entries and optional ESP32 Preferences persistence.

### `Calculator`

Safe arithmetic expression parsing with no code execution.

### `TelemetryEngine`

Collects target-reported ESP32 data and provides explicit state/confidence metadata.

### `HealthEngine`

Calculates an explainable health result from available telemetry only.

## Examples

- `BasicChat`
- `TrainKnowledge`
- `GeneralKnowledge`
- `Calculator`
- `ESP32Diagnostics`
- `WiFiDiagnostics`
- `MemoryDiagnostics`
- `PerformanceMonitor`
- `SmartHardwareChat`
- `FullDemo`

## Testing and validation

A host-side C++ validation harness is included under `tests/`. It exercises normalization, duplicate/near-duplicate rejection, calculator safety, confidence behavior and the no-hardware fallback path. This does **not** constitute physical ESP32 testing.

The build environment used for this package did not contain Arduino CLI or PlatformIO, so Arduino-ESP32 compile verification could not be performed here.

## Limitations

This is an offline-first embedded library, not a cloud LLM. Knowledge answers come from the bounded trained data bundled or added by the application. It does not invent facts from a remote model. Peripheral throughput and signal-quality metrics that require active instrumentation are not reported until a target-specific implementation can establish a real measurement.

## Troubleshooting

If an ESP32 capability returns `UNAVAILABLE` or `UNSUPPORTED`, check the installed Arduino-ESP32 core, selected board variant, and whether that target exposes the required API. Do not substitute zero for an unsupported measurement.

## Contribution guidance

Keep target-specific code isolated, add capability checks before new measurements, document measurement methodology, and add a test for every new routing or validation rule.

## License

MIT. See `LICENSE`.

## Release verification

Host C++17 validation: **PASS** (normalization, duplicate detection, calculator, Q&A routing, unavailable-hardware fallback).

Arduino-ESP32 compilation: **NOT VERIFIED** in the packaging environment because no Arduino CLI/PlatformIO ESP32 toolchain was installed.

Physical ESP32 testing: **NOT PERFORMED**.
