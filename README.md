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
/*
 * AmelTech lab's bot - Complete working sketch
 * Compatible with your current library version
 *
 * Features:
 *   - Ask any question (knowledge)
 *   - Calculator
 *   - Live hardware / telemetry questions
 *   - Health score
 *   - Full diagnostics
 *   - Train custom knowledge from Serial
 *   - Save / clear user knowledge (NVS)
 *   - Status + confidence
 *   - Help menu
 *
 * Serial commands:
 *   help
 *   <any question>
 *   25 * 4
 *   health
 *   diag
 *   train | question | answer
 *   save
 *   clear
 *   status
 */

#include <AmelTechBot.h>

AmelTechBot bot;
bool botReady = false;

// -------------------------------------------------
// Help menu
// -------------------------------------------------
void printHelp() {
  Serial.println();
  Serial.println("============== AmelTech Bot ==============");
  Serial.println("  help                 Show this help");
  Serial.println("  <any question>       Ask knowledge / hardware");
  Serial.println("  25 * 4               Calculator");
  Serial.println("  health               Health score report");
  Serial.println("  diag                 Full diagnostics");
  Serial.println("  train | q | a        Train custom knowledge");
  Serial.println("  save                 Save user knowledge (NVS)");
  Serial.println("  clear                Clear user knowledge");
  Serial.println("  status               Last status + confidence");
  Serial.println("==========================================");
  Serial.println();
}

// -------------------------------------------------
// Setup
// -------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================");
  Serial.println("   AmelTech Bot Starting...");
  Serial.println("==============================");

  botReady = bot.begin();

  if (!botReady) {
    Serial.print("Bot failed to start: ");
    Serial.println(bot.getLastStatus());
    return;
  }

  Serial.println("Bot ready!");
  printHelp();

  Serial.println("Examples:");
  Serial.println("  what is esp32");
  Serial.println("  what is free heap");
  Serial.println("  25 * 4");
  Serial.println("  health");
  Serial.println("  diag");
  Serial.println("  train | question | answer");
  Serial.println("  save");
  Serial.println();
}

// -------------------------------------------------
// Main loop
// -------------------------------------------------
void loop() {
  if (!botReady) {
    delay(1000);
    return;
  }

  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  Serial.print("\n> ");
  Serial.println(line);

  // ---------- HELP ----------
  if (line.equalsIgnoreCase("help")) {
    printHelp();
    return;
  }

  // ---------- STATUS ----------
  if (line.equalsIgnoreCase("status")) {
    Serial.print("Last status : ");
    Serial.println(bot.getLastStatus());
    Serial.print("Confidence  : ");
    Serial.println(bot.getConfidence(), 2);
    return;
  }

  // ---------- SAVE ----------
  if (line.equalsIgnoreCase("save")) {
    bot.saveKnowledge();
    Serial.print("Save result : ");
    Serial.println(bot.getLastStatus());
    return;
  }

  // ---------- CLEAR ----------
  if (line.equalsIgnoreCase("clear")) {
    bot.clearKnowledge();
    Serial.println("User knowledge cleared.");
    Serial.println("(Built-in knowledge is kept)");
    return;
  }

  // ---------- HEALTH ----------
  // Uses ask() so it always returns a String (safe for all library versions)
  if (line.equalsIgnoreCase("health")) {
    Serial.println(bot.ask("what is the health"));
    Serial.print("Confidence: ");
    Serial.println(bot.getConfidence(), 2);
    return;
  }

  // ---------- DIAGNOSTICS ----------
  if (line.equalsIgnoreCase("diag") ||
      line.equalsIgnoreCase("diagnostics") ||
      line.equalsIgnoreCase("run diagnostics")) {
    Serial.println(bot.ask("run diagnostics"));
    Serial.print("Confidence: ");
    Serial.println(bot.getConfidence(), 2);
    return;
  }

  // ---------- TRAIN ----------
  // Format: train | question | answer
  if (line.startsWith("train |") || line.startsWith("train|")) {
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);

    if (p1 < 0 || p2 < 0) {
      Serial.println("Bad format.");
      Serial.println("Use: train | your question | your answer");
      return;
    }

    String q = line.substring(p1 + 1, p2);
    String a = line.substring(p2 + 1);
    q.trim();
    a.trim();

    if (q.length() == 0 || a.length() == 0) {
      Serial.println("Question and answer cannot be empty.");
      return;
    }

    bot.train(q, a, "custom");
    Serial.print("Train result : ");
    Serial.println(bot.getLastStatus());
    Serial.println("Type 'save' to keep it after reboot.");
    return;
  }

  // ---------- NORMAL ASK ----------
  // Handles: knowledge, calculator, telemetry questions
  String answer = bot.ask(line);
  Serial.println(answer);

  Serial.print("Confidence: ");
  Serial.println(bot.getConfidence(), 2);
  Serial.println("------------------------------");
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

## AmelTech lab's bot

**Offline ESP32 knowledge engine** with calculator, telemetry, diagnostics, and explainable health scoring.

No cloud. No network required for core operation. Built for Arduino-ESP32.

Repository: [ameltechlabs/AmelTech-labs-bot](https://github.com/ameltechlabs/AmelTech-labs-bot)

---

## What it does

AmelTech lab's bot is a single Arduino library that turns an ESP32 into a small offline assistant. You can:

- Ask general-knowledge and hardware questions over Serial (or your own UI)
- Run a safe math calculator
- Query live ESP32 telemetry (heap, RSSI, uptime, temperature when available, …)
- Run diagnostics and get an explainable **health score** (0–100)
- Train custom Q&A that persists in NVS
- Keep short conversation context for simple follow-ups

Matching is deterministic / heuristic (exact → normalized → keyword → fuzzy). It is **not** a neural network.

---

## Features

| Area | Capability |
|------|------------|
| **Knowledge** | 2039 built-in Q&A entries embedded in flash |
| **Matching** | Exact, normalized, keyword, fuzzy + confidence bands |
| **Training** | User `train()` / `addQA()` with duplicate & conflict detection |
| **Persistence** | User knowledge saved/loaded via ESP32 NVS |
| **Calculator** | `+ - * / %`, parentheses, decimals, operator precedence |
| **Context** | Bounded follow-up memory (up to 4 turns) |
| **Telemetry** | Explicit statuses: LIVE, CACHED, STALE, UNAVAILABLE, UNSUPPORTED, ERROR |
| **Diagnostics** | Text hardware report |
| **Health score** | 5-component score (CPU, Memory, Wi-Fi, Communication, System) → 0–100 |
| **Trolling** | Optional harmless humor mode |
| **API** | One public header: `#include <AmelTechBot.h>` |

---

## Built-in knowledge categories (2039 entries)

Questions are grouped by category. The engine uses the category for matching priority and conflict resolution.

### General knowledge

| Category | Count | Example topics |
|----------|------:|----------------|
| `gk` | 1136 | Broad general knowledge |
| `science` | 28 | Physics, chemistry basics |
| `computing` | 78 | Computers, software concepts |
| `math` | 14 | Math terms and facts |
| `history` | 1 | Historical facts |
| `geography` | 1 | Places and geography |
| `earth_gk` | 25 | Earth science |
| `space_gk` | 20 | Space and astronomy |
| `disaster_hazard_gk` | 25 | Disasters, hazards, safety |
| `cybersecurity` | 5 | Security basics |
| `electromagnetic` | 14 | EM spectrum, waves |

### Electronics, Arduino & ESP32

| Category | Count | Example topics |
|----------|------:|----------------|
| `esp32` | 88 | ESP32 chip, features, APIs |
| `esp32_choice` | 20 | Which ESP32 variant to pick |
| `esp32_projects` | 20 | Project ideas |
| `arduino` | 38 | Arduino platform |
| `arduino_choice` | 10 | Board selection |
| `arduino_projects_gk` | 20 | Arduino project ideas |
| `electronics` | 68 | Components, circuits |
| `sensor` | 61 | Sensors and readings |
| `sensor_module_choice` | 30 | Which sensor module to use |
| `networking` | 17 | Wi-Fi, networking basics |
| `chemistry_table` | 34 | Elements / chemistry reference |

### AI, meta & library itself

| Category | Count | Example topics |
|----------|------:|----------------|
| `ai_use` | 20 | Practical AI usage |
| `ai_model_choice` | 15 | Choosing AI models |
| `ameltechbot_features` | 46 | What this library can do |
| `library_creator` | 13 | About the library |
| `creator_profile` | 7 | Creator info |
| `meta` | 18 | Self-describing / meta questions |

### Conversation & fun

| Category | Count | Example topics |
|----------|------:|----------------|
| `greeting` | 14 | hi, hello, good morning |
| `goodbye` | 7 | bye, see you |
| `friendly` | 25 | Polite / friendly replies |
| `funny` / `fun` | 5 | Light humor |
| `funny_trolling` | 19 | Optional sarcastic mode (cleaned, respectful) |
| `mock` | 49 | Playful mock answers |
| `love` | 9 | Affection-style replies |
| `dreams` | 7 | Dream-related |
| `pizza_making` | 15 | How to make pizza (fun domain) |
| `indian_cricket_player_profile` | 15 | Cricket player profiles |
| `important` | 2 | High-priority facts |

> Categories with specific domain names (e.g. `science`, `electronics`) are preferred over generic `gk` when the same question appears in both.



 ### Serial commands:
 *   any question          -> bot.ask()
 *   train | q | a         -> train custom knowledge
 *   save                  -> save user knowledge to NVS
 *   health                -> health report
 *   diag                  -> full diagnostics
 *   help                  -> show commands

---

## Types of questions the bot understands

### 1. Knowledge questions

```text
what is esp32
what is free heap
who is sunil chhetri
what is gravity
how does a resistor work

## Custom trained knowledge

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
