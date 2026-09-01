# AmelTech lab's bot

**Version 2.0.0** — an offline chatbot library for the ESP32 family.
It answers general knowledge questions, solves maths, reads DHT sensors and
talks about the board it runs on. It remembers who you are between power
cycles, learns from you over the serial monitor, and reports its own health.

Everything runs on the chip. No network, no cloud, no API key, no subscription.


**Offline-capable ESP32 Arduino library**  
Knowledge engine + Calculator + Telemetry + Diagnostics + Health scoring

Repository: [ameltechlabs/AmelTech-labs-bot](https://github.com/ameltechlabs/AmelTech-labs-bot)

---

## Features

- Fully offline question–answer engine (no internet required)
- Built-in knowledge base (embedded in flash)
- Trainable user knowledge with NVS persistence
- Safe expression calculator (`+ - * / %`, parentheses, decimals)
- Input normalization + exact / keyword / fuzzy matching
- Confidence scoring
- Conversation context
- ESP32 hardware telemetry (heap, uptime, RSSI, temperature, etc.)
- Diagnostics and explainable health score
- Optional trolling mode
- Single header: `#include <AmelTechBot.h>`

---

## Supported Platforms

- ESP32 (classic)
- ESP32-S2 / S3
- ESP32-C3 / C6
- ESP32-H2

Unsupported features are clearly reported as `UNSUPPORTED` or `UNAVAILABLE`.

---

## Important Note (Flash Size)

This library contains a large built-in knowledge base.  
The compiled binary is approximately **1.3 – 1.4 MB**.

**You must change the Partition Scheme**, otherwise you will get this error:

### Required Setting:

**Tools → Partition Scheme → No OTA (2MB APP/2MB SPIFFS)**

or

**Huge APP (3MB No OTA)**

---

## Installation

1. Download the library ZIP
2. In Arduino IDE go to:  
   **Sketch → Include Library → Add .ZIP Library…**
3. Select an ESP32 board
4. Set Partition Scheme as shown above
5. Open any example

---

## Quick Start (Recommended Code)

```cpp
#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(1000);                // Important: wait for Serial

  Serial.println("==============================");
  Serial.println("   AmelTech Bot Starting...");
  Serial.println("==============================");

  bot.begin();

  Serial.println("Bot is ready!");
  Serial.println("Type your question and press Enter");
  Serial.println("----------------------------------");
}

void loop() {
  if (Serial.available()) {
    String question = Serial.readStringUntil('\n');
    question.trim();                    // remove extra spaces / Enter

    if (question.length() > 0) {
      String answer = bot.ask(question);
      Serial.println(answer);
      Serial.println("----------------------------------");
    }
  }
}
```

```
> what is wifi
Wi-Fi is a family of wireless networking technologies based on IEEE 802.11
standards that allow devices to exchange data over radio waves.

> what is 15% of 200
30

> how is the room
Comfortable, if slightly humid.
Temperature 24.3 C, humidity 63%, feels like 24.9 C, dew point 16.8 C.
Comfort: ideal. Temperature is steady, humidity is rising.
Keep an eye on the humidity if it climbs much further.
I have counted the water in this air. There is a lot of it.

> hi my name is Joky Pk
Nice to meet you, Joky Pk. I will remember your name.

> train | who made you | AmelTech labs made me.
train successfully and save data number code 0001
```

---

## What is new in version 2

| Area | v1.1.0 | v2.0.0 |
|---|---|---|
| Matching | full scan, Levenshtein on every row | two-stage prefilter, tens of microseconds |
| Wording | exact wording usually required | generalises over phrasing and typos |
| Maths | four operators, silent truncation over 95 chars | 30+ functions, powers, factorials, percentages, constants |
| Sensors | none | DHT11 / DHT21 / DHT22 with situation analysis |
| Memory of people | none | 34 names and professions, survives a power cycle |
| Training | `train()` from code only | serial console with data numbers and a heap guard |
| Health score | unweighted mean of 5 numbers | confidence-weighted over 7 components |
| Thermal | none | throttles and yields before the chip gets hot |
| Flash use | knowledge table duplicated per translation unit | single definition |

See [CHANGELOG.md](CHANGELOG.md) for the full list, including every defect that
was fixed.

---

## Installation

**Arduino IDE**

1. Download this repository as a ZIP.
2. *Sketch → Include Library → Add .ZIP Library…*
3. *File → Examples → AmelTech lab's bot → BasicChat*

**PlatformIO**

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = file://path/to/AmelTech-labs-bot
monitor_speed = 115200
```

**Requirements**

- An ESP32, ESP32-S2, ESP32-S3, ESP32-C3 or ESP32-C6 board
- Arduino ESP32 core 2.0.0 or newer
- Around 500 KB of flash for the built-in knowledge, and roughly 30 KB of RAM
- A partition scheme with at least 1.2 MB of app space (the default "Huge APP"
  or "Minimal SPIFFS" schemes both work)

No other libraries are needed. DHT support is built in.

---

## What it can do

### General knowledge

2037 built-in entries covering science, electronics, computing, geography,
history and everyday facts. The matcher generalises over wording, so all of
these find the same answer:

```
what is wifi        What is Wi-Fi?      whats wi-fi
tell me about wifi  explain wifi        define wifi
```

Typos are tolerated. `what is teh speed of light` still resolves.

When the bot is not confident it says so and offers the closest topics instead
of inventing an answer. That behaviour is deliberate and is not configurable.

### Maths

```
25 * 4              (12 + 8) / 5        2^10            -2^2
7!                  sqrt(144)           cbrt(27)        |-5|
15% of 200          200 + 10%           3(4 + 5)        2pi
log2(256)           hypot(3, 4)         gcd(48, 18)     1.5e3 + 500
sin(pi / 2)         atan2(1, 1)         mod(10, 3)      what is 7 squared
```

Over thirty functions, the constants `pi`, `e`, `tau` and `phi`, implicit
multiplication, scientific notation, right-associative powers, and a choice of
radians or degrees. Division by zero, domain errors and malformed input are
reported rather than guessed at.

### Sensors

DHT11, DHT21 and DHT22 are supported directly, with no external library. The
bot computes dew point, heat index, absolute humidity, a comfort rating and an
eight-sample trend, then warns about condensation, mould, dry air and
conditions the board itself will not enjoy.

If the sensor does not answer, the bot says so. It never substitutes the last
reading for a live one without labelling it, and it never makes a number up.

See [docs/SENSORS.md](docs/SENSORS.md) for wiring and the full analysis.

### Remembering people

```
> hi my name is Joky Pk
Nice to meet you, Joky Pk. I will remember your name.

  ... reset the board ...

> what is the speed of light
Are you Joky Pk?
> yes
Good to see you again, Joky Pk. The speed of light in vacuum is approximately
299,792,458 meters per second, Joky Pk.
```

Up to 34 names, each with an optional profession, stored in NVS. After four
"no" answers the bot stops guessing and asks for your name outright.

See [docs/IDENTITY.md](docs/IDENTITY.md).

### Training over the serial monitor

```
train | who made you | AmelTech labs made me.
  -> train successfully and save data number code 0001

train | list
train | status
train | delete | 0001
train | delete | full data
```

Training is refused while free heap is at or below the reserved minimum
(200 KB by default), which is what keeps chat logging and the matcher alive.
The console explains the situation instead of failing quietly.

See [docs/TRAINING.md](docs/TRAINING.md).

### Talking to the board

```
> how much free memory do you have
Free heap: 187 KB of 320 KB, 12% fragmented. Taught entries use 3 KB.

> how hot is the chip
This chip has no usable internal temperature sensor, so I cannot give you a
real die temperature. Ambient is 24.3 C, and the die usually runs 10-20 C
above that.

> what is your health score
ESP32 Health: 91/100 [NORMAL]  confidence 78%
```

Every measurement carries a status: `LIVE`, `CACHED`, `STALE`, `UNAVAILABLE`,
`UNSUPPORTED` or `ERROR`. Nothing is ever presented as measured when it was
not. On the classic ESP32 the internal temperature sensor returns a fixed
53.33 °C placeholder; the library detects that and reports `UNSUPPORTED`
rather than passing the placeholder off as a reading.

See [docs/TELEMETRY.md](docs/TELEMETRY.md) and [docs/STATUS.md](docs/STATUS.md).

---

## Examples

| Sketch | What it shows |
|---|---|
| `BasicChat` | the smallest useful sketch |
| `Calculator` | the full range of maths |
| `GeneralKnowledge` | wording, typos and confidence |
| `NeuralMatching` | what the matcher sees, and how fast |
| `SmartHardwareChat` | asking the board about itself |
| `ESP32Diagnostics` | the full diagnostic sweep |
| `MemoryDiagnostics` | heap use and the training reserve |
| `WiFiDiagnostics` | signal reporting, and working without it |
| `PerformanceMonitor` | timing, cache and thermal duty |
| `TrainKnowledge` | teaching from code |
| `SerialTraining` | the training console on its own |
| `IdentityMemory` | name memory across a reset |
| `DhtTrolling` | DHT sensing, analysis and humour |
| `FullDemo` | all of the above together |

---

## A note on "neural"

The matcher embeds text into a fixed 48-dimensional vector and blends five
similarity measures. That is what lets it generalise over wording and survive
typos, and it is why the behaviour feels like a small language model.

It is **not** a trained network. The projection is deterministic feature
hashing, the weights are fixed constants chosen by hand, and no training data
was involved. The consequences matter more than the label:

- the same question always produces the same answer,
- the bot cannot invent a fact, because every answer is a stored one,
- it runs in tens of microseconds inside 30 KB of RAM.

On a microcontroller those properties are worth far more than fluency.

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains exactly how the scoring
works.

---

## Documentation

- [docs/API.md](docs/API.md) — every public method
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the matcher works
- [docs/TRAINING.md](docs/TRAINING.md) — the training console
- [docs/IDENTITY.md](docs/IDENTITY.md) — name memory
- [docs/SENSORS.md](docs/SENSORS.md) — DHT wiring and analysis
- [docs/TELEMETRY.md](docs/TELEMETRY.md) — every telemetry field
- [docs/STATUS.md](docs/STATUS.md) — measurement statuses and health scoring

---

## Testing

The library builds and runs on a desktop, which makes the logic testable
without hardware:

```bash
cd tests
cmake -B build .
cmake --build build
./build/ameltech_tests
```

Or without CMake, from the library root:

```bash
g++ -std=c++17 -O1 -DAMELTECH_HOST_NVS -Itests/host_stub -Isrc \
    tests/host_test.cpp tests/host_stub/host_stub.cpp src/*.cpp -o ameltech_tests
./ameltech_tests
```

243 checks cover the normalizer, the matcher, the calculator, name extraction,
the identity state machine, the training console, persistence, the DHT maths
and the health scoring.

To regenerate the built-in knowledge table after editing `data/knowledge.json`:

```bash
python3 tools/generate_knowledge.py --selftest   # verify the normalizer first
python3 tools/generate_knowledge.py
```

The generator mirrors the C++ normalizer exactly. `--selftest` proves it, which
matters: in v1 the two disagreed and every affected entry silently lost its
exact match.

---

## Configuration

Everything tunable lives in `src/AmelTechConfig.h` and can be overridden with a
`-D` flag before including the library. The ones people change most often:

```cpp
#define AMELTECH_TRAIN_MIN_FREE_HEAP  (200UL * 1024UL)  // training reserve
#define AMELTECH_MAX_PROFILES         34                // remembered names
#define AMELTECH_MAX_USER_ENTRIES     48                // taught entries
#define AMELTECH_IDENTITY_MAX_GUESSES 4                 // name guesses
#define AMELTECH_NAME_MENTION_GAP     3                 // replies between names
#define AMELTECH_THERMAL_WARN_C       70.0f
```

---

## Licence

MIT. See [LICENSE](LICENSE).

Built by AmelTech labs.
