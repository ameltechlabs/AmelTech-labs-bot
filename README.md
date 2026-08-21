# AmelTech lab's bot

**Offline-capable ESP32 Arduino library** combining a knowledge engine, calculator, intent detection, telemetry, diagnostics, and health scoring.

Repository: [ameltechlabs/AmelTech-labs-bot](https://github.com/ameltechlabs/AmelTech-labs-bot)

## Features

- Offline question–answer engine (no network required for core operation)
- Trainable user knowledge base with duplicate / contradiction checks
- Built-in knowledge embedded in flash (from `data/knowledge.json`)
- Safe expression calculator (`+ - * / %`, parentheses, decimals)
- Input normalization, tokenization, exact / keyword / fuzzy matching
- Lightweight similarity scoring and confidence bands
- Bounded conversation context
- ESP32 hardware telemetry with explicit status (LIVE / UNAVAILABLE / UNSUPPORTED / …)
- Diagnostics and explainable health score
- Optional harmless trolling mode
- NVS persistence for user knowledge
- Single public header: `#include <AmelTechBot.h>`

## Supported platforms

- Architectures: `esp32` (Arduino-ESP32 core)
- Families accounted for: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-H2 and related variants where APIs exist
- Capability differences (Bluetooth, DAC, temperature sensor, PSRAM, etc.) are reported as **UNSUPPORTED** when not available — values are never fabricated

## Installation

1. Download the library ZIP or clone the repository
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library…** (or place the folder under `libraries/`)
3. Select an ESP32 board and open an example

## Quick start

```cpp
#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();
  Serial.println("AmelTech bot ready. Type a question:");
}

void loop() {
  if (Serial.available()) {
    String q = Serial.readStringUntil('\n');
    q.trim();
    if (q.length() > 0) {
      Serial.print("> ");
      Serial.println(q);
      Serial.println(bot.ask(q));
    }
  }
}
```

Built-in knowledge is supplied by the library. You do **not** need to call `train()` for the default Q&A set.

## Knowledge workflow

```
data/knowledge.json
        ↓  tools/generate_knowledge.py
src/knowledge_generated.h
        ↓  compile
ESP32 firmware
```

Regenerate after editing the JSON:

```bash
python3 tools/generate_knowledge.py
```

## Custom training

```cpp
bot.train("what is my project", "My project uses an ESP32.", "custom");
bot.saveKnowledge();  // NVS
```

## Calculator

```cpp
bot.calculate("25 * 4");
bot.ask("(25 + 5) * 2");
```

## Telemetry & diagnostics

```cpp
Serial.println(bot.ask("What is free heap?"));
Serial.println(bot.ask("What is my Wi-Fi RSSI?"));
Serial.println(bot.runDiagnostics(true));
Serial.println(bot.getHealthReport());
```

Unsupported measurements return explicit `UNSUPPORTED` / `UNAVAILABLE`.

## Confidence

| Score   | Meaning                |
|---------|------------------------|
| ≥ 0.90  | Strong answer          |
| 0.75–0.89 | Moderate             |
| 0.50–0.74 | Clarification preferred |
| < 0.50  | Unknown                |

## Examples

- BasicChat, TrainKnowledge, GeneralKnowledge, Calculator
- ESP32Diagnostics, WiFiDiagnostics, MemoryDiagnostics
- PerformanceMonitor, SmartHardwareChat, FullDemo

## Limitations

- Not a neural network — matching is deterministic / heuristic
- Internal temperature and some peripherals vary by chip; may be UNSUPPORTED
- Wi-Fi throughput is not claimed unless actually measured
- Host tests do not exercise real ESP32 hardware APIs

## Testing status

See [docs/STATUS.md](docs/STATUS.md).

## License

MIT — see [LICENSE](LICENSE)
