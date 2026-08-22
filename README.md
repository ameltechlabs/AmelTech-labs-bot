# AmelTech lab's bot

An offline-capable ESP32 Arduino library combining a trainable
question-answer engine, a safe embedded calculator, bounded conversation
context, and evidence-based ESP32 hardware telemetry/diagnostics/health
scoring — all behind a single public header.

```cpp
#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    bot.begin();

    Serial.println(bot.ask("What is water?"));
    Serial.println(bot.ask("What is ESP32?"));
    Serial.println(bot.ask("How many seconds are in one minute?"));
}

void loop() {}
```

No built-in Q&A needs to be added by your sketch — the knowledge ships
inside the library itself.

## Features

- Offline question-answer engine: normalization, tokenization, exact
  matching, keyword matching, bounded fuzzy matching, and a lightweight
  (non-neural) token-overlap + edit-distance similarity blend.
- Trainable knowledge base: add your own facts at runtime, separate from
  built-in knowledge, with duplicate and contradiction detection.
- Safe embedded calculator: `+ - * / % ( )`, decimals, percentages,
  operator precedence — no arbitrary code execution.
- Bounded conversation context for simple follow-ups ("is that good?").
- Confidence-scored answers — never a fabricated guess; low-confidence
  questions get an honest "I don't know" or a clarification prompt.
- ESP32 hardware telemetry: chip/CPU/memory/Wi-Fi/Bluetooth/UART/I2C/SPI/
  GPIO/ADC/DAC/PWM/temperature/watchdog/system, every field explicitly
  marked `LIVE`/`CACHED`/`UNAVAILABLE`/`UNSUPPORTED`/`MEASUREMENT_ERROR` —
  never fabricated, never zero-as-unsupported.
- Explainable health scoring built only from actual measurements.
- Optional, harmless "trolling mode" commentary — always secondary to the
  real answer, never replacing a warning.
- Persistent user knowledge via ESP32 NVS (`Preferences`).
- Full error/status enum exposed through the public API.

## Architecture

```
User Sketch -> #include <AmelTechBot.h> -> AmelTechBot
                                              +-- KnowledgeBase
                                              +-- Calculator
                                              +-- Telemetry
                                              +-- Diagnostics
```

See `docs/ARCHITECTURE.md` for the full design and matching pipeline, and
`docs/TELEMETRY.md` for the complete field-by-field telemetry reference.

## Supported ESP32 families

ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2, detected at
compile time. Capability differences (DAC availability, GPIO counts, BT
support, etc.) are explicitly accounted for — see `docs/TELEMETRY.md`.

## Installation

1. Copy (or clone) this repository into your Arduino `libraries/` folder
   as `AmelTech-labs-bot`, **or** install the release ZIP via
   Arduino IDE → Sketch → Include Library → Add .ZIP Library.
2. Restart the Arduino IDE if it was already open.
3. Open any example from `File → Examples → AmelTech lab's bot`.

Requires an ESP32 board definition installed in your Arduino IDE /
PlatformIO environment (`architectures=esp32` in `library.properties`).

## Quick start

```cpp
#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    bot.begin();
    Serial.println(bot.ask("What is ESP32?"));
}

void loop() {}
```

## Built-in knowledge

30 built-in entries across `science`, `math`, `esp32`, and `general`
categories ship inside `src/knowledge_generated.h`, compiled from
`data/knowledge.json` and stored flash-resident (`PROGMEM`-qualified) so
they don't consume RAM at rest. See that file for the exact list.

## Custom training

```cpp
AmelTechStatus status = bot.train(
    "what is my project",
    "My project uses an ESP32 and multiple sensors.",
    "custom"
);

if (status == AMELTECH_OK) {
    Serial.println(bot.ask("What is my project?"));
} else {
    Serial.println(ameltechStatusToString(status));
}

bot.saveKnowledge(); // persist to NVS
```

Training validates empty questions/answers, length limits, invalid
categories, exact duplicates, and contradictions (a different answer
already stored for the same or a near-duplicate question) — it never
silently overwrites existing knowledge.

## Knowledge dataset workflow

```
data/knowledge.json
        |  (edit this — never hand-edit the generated header)
        v
tools/generate_knowledge.py   (validates + generates)
        v
src/knowledge_generated.h     (flash-resident, PROGMEM-qualified)
        v
AmelTechBot -> ESP32 firmware
```

```bash
python3 tools/generate_knowledge.py \
    --input data/knowledge.json \
    --output src/knowledge_generated.h
```

The generator validates JSON structure, required fields, empty
questions/answers, exact and normalized duplicates, invalid categories,
and maximum entry lengths (must match `AMELTECH_MAX_QUESTION_LEN`=96 /
`AMELTECH_MAX_ANSWER_LEN`=220 in the C++ headers).

## Calculator

```cpp
Serial.println(bot.ask("(25 + 5) * 2"));   // 60.00
CalcResult r = bot.calculate("10 / 0");    // r.valid == false, CALC_ERROR_DIV_BY_ZERO
```

## Telemetry

```cpp
ESP32Telemetry fast = bot.getTelemetry(false);  // cheap fields
ESP32Telemetry full = bot.getTelemetry(true);   // + flash/PSRAM/I2C scan/GPIO inventory

if (fast.memory.freeHeapBytes.status == MEAS_LIVE) {
    Serial.println(fast.memory.freeHeapBytes.value);
}
```

Natural-language hardware questions are also routed to real telemetry:

```cpp
Serial.println(bot.ask("What is my Wi-Fi RSSI?"));
Serial.println(bot.ask("What is my free heap?"));
Serial.println(bot.ask("What is the ESP32 health?"));
```

## Diagnostics & health engine

```cpp
DiagnosticsReport report = bot.runDiagnostics(true); // full scan
Serial.println(report.summary); // "Health: 88/100 (NORMAL) | Main issue: ..."
```

Each subsystem (CPU, Memory, Wi-Fi, Communication, System) is only scored
if it was actually measured; unmeasured subsystems are `UNKNOWN` and
excluded from the average rather than penalized or guessed.

## Confidence system

| Tier | Range | Behavior |
|---|---|---|
| Strong | ≥ 0.90 | Direct answer |
| Moderate | 0.75–0.89 | Direct answer |
| Clarify | 0.50–0.74 | "Did you mean...?" style response |
| Unknown | < 0.50 | "I don't have enough reliable information to answer that." |

## Performance & memory considerations

- Fixed-size buffers throughout; user knowledge capped at 64 entries;
  context capped at 8 entries.
- Built-in knowledge is read from flash on demand, not copied into RAM.
- Bounded, early-exiting Levenshtein distance (128-character hard cap).
- Two-speed telemetry keeps expensive operations (I2C scan, flash/PSRAM
  queries) off the default fast path.
- No arbitrary code execution anywhere (calculator is a bounded
  recursive-descent parser, not `eval`).

## API reference

See `docs/API.md` for the complete public method list, error codes, and
data structures.

## Examples

`examples/` contains ten sketches: `BasicChat`, `TrainKnowledge`,
`GeneralKnowledge`, `Calculator`, `ESP32Diagnostics`, `WiFiDiagnostics`,
`MemoryDiagnostics`, `PerformanceMonitor`, `SmartHardwareChat`, `FullDemo`.

## Limitations

- Similarity matching is a lightweight, deterministic token-overlap +
  edit-distance blend — **not** a trained neural/embedding model.
- Hardware telemetry can only report what the target chip and Arduino-ESP32
  core actually expose; unsupported fields are marked as such rather than
  estimated.
- Wi-Fi/Bluetooth/UART/SPI throughput figures are never fabricated from
  configured rates; they require an explicit benchmark (not run
  implicitly) and report `UNAVAILABLE` until one exists.
- Context memory handles a small, fixed set of deictic references ("that",
  "it", "this") — it is not a general coreference resolver.

## Troubleshooting

- **`ask()` always returns "not initialized"**: call `bot.begin()` in
  `setup()` before any `ask()`/`train()` call.
- **Telemetry fields show `UNAVAILABLE`**: many fields require a full
  scan (`getTelemetry(true)`) or a prior sketch-side setup step (e.g.
  `Serial.begin()` before UART baud rate is known, or Wi-Fi connection
  before RSSI is available).
- **`saveKnowledge()`/`loadKnowledge()` return `AMELTECH_UNSUPPORTED`**:
  these require the ESP32 `Preferences`/NVS API and only work on actual
  ESP32 hardware, not host builds.
- **Training returns `AMELTECH_CONTRADICTION`**: a different answer is
  already stored for that (or a near-duplicate) question. Call
  `removeQA()` first if you intend to replace it.

## Testing status

Host-side unit tests (`tests/host_test.cpp`) cover normalization,
matching, training/duplicate/contradiction detection, confidence
thresholds, and calculator behavior: **34/34 passing** on g++ 13.3.0
(C++14, `-Wall -Wextra`, zero warnings). ESP32 hardware/toolchain
compilation was **not** performed in this environment — see
`docs/STATUS.md` for the complete, honest status table and reasoning.

## Contribution guidance

Issues and PRs welcome. Please:
- Edit `data/knowledge.json`, not `src/knowledge_generated.h` directly,
  and re-run `tools/generate_knowledge.py`.
- Add or update host tests in `tests/host_test.cpp` for logic changes.
- Keep telemetry additions gated behind explicit capability checks with
  documented non-`LIVE` statuses for unsupported paths — never fabricate
  a hardware value.

## License

MIT — see `LICENSE`.
