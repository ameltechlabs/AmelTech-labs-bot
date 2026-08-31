# Architecture

How the library is put together, and why.

---

## Module layout

```
AmelTechBot          orchestration: routes a question through the pipeline
├── NeuralEngine     text normalization, embedding, similarity scoring
├── KnowledgeBase    2037 built-in entries + up to 48 taught ones, two-stage search
├── Calculator       recursive descent expression parser
├── SensorHub        DHT driver and situation analysis
├── Telemetry        hardware readings, each with a status
├── ThermalGuard     temperature state, throttling, cooperative yielding
├── Diagnostics      weighted health scoring
├── UserProfileStore 34 name slots in NVS
├── IdentityManager  the "Are you X?" conversation
├── TrainingConsole  the serial training commands
└── AmelTechLog      heap-aware ring buffer
```

Each module owns one concern and can be used on its own. `AmelTechBot` is the
only thing that knows the order they run in.

---

## Answer pipeline

A question passes through these stages, stopping at the first that handles it:

1. **Guards** — empty input, over-length input, not started
2. **Identity** — boot confirmation, yes/no answers, name capture
3. **Self query** — "who are you", "what can you do", "what version"
4. **Small talk** — greetings, thanks, goodbyes, "how are you"
5. **Maths** — anything the calculator recognises as an expression
6. **Sensors** — temperature, humidity, room conditions
7. **Hardware** — heap, CPU, Wi-Fi, uptime, chip, health, diagnostics
8. **Follow-up** — "tell me more", "why", "explain that"
9. **Knowledge base** — with query rewriting on low confidence
10. **Fallback** — an honest non-answer plus the closest topics

Then post-processing: humour on sensor answers, the user's name at the mention
gap, context push, and a thermal tick.

Ordering matters. Maths runs before the knowledge base so `what is 25*4` is
calculated rather than searched, but after small talk so "hi" is not parsed as
an expression. Sensor questions run before hardware ones so "what is the
temperature" means the room, while "what is the chip temperature" means the die.

---

## The matcher

### The problem

2037 entries. The v1 approach ran Levenshtein distance and re-tokenized every
row on every query, which took several milliseconds. On a microcontroller that
is a watchdog risk and, run continuously, a source of heat.

### Stage 1: prefilter

Every stored row carries two precomputed sketches:

- **`uint32_t signature`** — a token-hash bitset. Each token sets one of 32
  bits. Comparing two signatures is one AND and one popcount.
- **`uint64_t bloom`** — a character-trigram sketch. Each trigram sets one of
  64 bits, which captures spelling, so typos still overlap.

The sweep computes a cheap score from both, skipping any row whose signature
shares no bits with the query. This is two popcounts and a branch per row —
fast enough to touch all 2037 rows in microseconds.

The top `AMELTECH_CANDIDATE_POOL` (12) rows go to stage 2.

### Stage 2: full scoring

Only those 12 get the expensive treatment:

```
score = 0.34 × token overlap
      + 0.24 × embedding cosine
      + 0.18 × trigram Dice
      + 0.16 × edit similarity
      + 0.08 × signature overlap
```

Plus small adjustments:

- **+0.07** when the query is contained in the candidate, or vice versa
- **±** for question-word agreement, so "what is X" prefers "what is X" over
  "how does X work"

### Adaptive fallback

If the best prefilter score is below 0.40, the query is probably misspelled
badly enough that token hashes do not line up. A second pass runs on bloom Dice
alone, which is spelling-based and survives typos. This is why
`waht is gravity` still resolves.

### Calibration

Raw scores are mapped through a piecewise-linear curve onto confidence bands:

| Band | Confidence | Behaviour |
|---|---|---|
| STRONG | ≥ 0.90 | answered directly |
| MODERATE | ≥ 0.74 | answered directly |
| WEAK | ≥ 0.52 | answered with a hedge: "I think you are asking about…" |
| below | < 0.52 | honest non-answer plus the nearest topics |

An exact normalized match short-circuits to 1.000.

### Query rewriting

The stored questions are canonical: "what is bluetooth". People type "tell me
about bluetooth". When the first attempt scores below MODERATE, the question is
retried in up to three canonical forms and the best result is kept:

```
tell me about bluetooth   →   what is bluetooth        (0.409 → 1.000)
explain i2c               →   what is i2c
define capacitor          →   what is capacitor
what does DHCP mean       →   what is dhcp
bluetooth                 →   what is bluetooth
```

### Measured performance

On an ESP32 at 240 MHz over 2037 entries:

| Query | Time | Confidence |
|---|---|---|
| `what is water` | 25 µs | 1.000 |
| `What is WiFi?` | 31 µs | 1.000 |
| `how many sec r there in 1 min` | 78 µs | 0.990 |
| `what is teh speed of light` | 96 µs | 0.924 |
| `waht is gravity` | 130 µs | 0.773 |
| unknown phrase | 118 µs | 0.155 |

Repeating a question hits the single-slot cache and costs almost nothing.

---

## Why "neural" but not a neural network

The embedding is deterministic feature hashing: each token is projected into a
48-dimensional vector by three FNV-1a hashes, signed by a fourth bit, then
L2-normalized. Cosine similarity between two such vectors behaves much like a
bag-of-words embedding, which is what lets the matcher generalise over wording.

There are no learned weights. The blend coefficients were chosen by hand and
tuned against the corpus. Nothing was trained, and nothing adapts at runtime.

That is a feature, not an apology:

- **Deterministic** — the same question always gives the same answer, which
  makes the library testable and debuggable.
- **Cannot hallucinate** — every answer is a stored string. The bot can be
  wrong about which entry you meant, and it reports its confidence when it is
  unsure, but it cannot fabricate a fact.
- **Small and fast** — 48 floats per query, tens of microseconds, no
  matrix multiply, no model file.

A real language model on this hardware would be slower, larger, and would make
things up. For a device that answers questions about physics and reports sensor
readings, none of those trades are worth making.

---

## Memory

### Flash

| Item | Size |
|---|---|
| Built-in knowledge table | ~466 KB |
| Library code | ~90 KB |

The knowledge table is declared in `knowledge_generated.h` and **defined once**
in `knowledge_generated.cpp`. In v1 it was defined in a header, so every
translation unit that included it got its own copy.

### RAM

| Item | Size |
|---|---|
| `AmelTechBot` and subsystems | ~6 KB static |
| Profile store (34 slots) | ~3 KB static |
| Log ring buffer | 1.3 KB, heap, lazily allocated |
| Taught entries | ~460 B each, heap, on demand |
| Query working set | ~2 KB stack |

Taught entries are uniform heap blocks rather than a fixed static array. v1
reserved 32 slots — about 21.5 KB — whether they were used or not.

The log buffer is allocated on first use and released under memory pressure.
Its counters survive, so the diagnostics still know how many errors occurred
even when the text is gone.

---

## Persistence

Two NVS namespaces:

| Namespace | Contents |
|---|---|
| `ameltech_kb` | taught entries, their categories and data numbers, the next code |
| `ameltech_id` | remembered names, professions and interaction counts |

Both use a dirty flag. `tick()` flushes at most once a minute, and `end()`
flushes on the way out. v1 wrote on every change, which wears flash quickly.

Both save routines remove keys left behind when the set shrinks. v1 did not,
so a deleted entry could reappear after a reboot.

Profiles are written in recency order, so a partial load still keeps the
newest names.

---

## Thermal and watchdog safety

`ThermalGuard` reads the die temperature where one exists and falls back to
ambient plus an offset when a DHT is attached, reporting which source it used.

| State | Threshold | Action |
|---|---|---|
| NORMAL | below 70 °C | none |
| WARM | 70 °C | longer cooperative yields |
| HOT | 80 °C | CPU throttled to 80 MHz |
| CRITICAL | 90 °C | throttled, aggressive yielding |

4 °C of hysteresis prevents oscillation at a boundary.

`beginSlice()` and `tick()` bracket the answer pipeline, and `AMELTECH_YIELD()`
runs between stages. Combined with the microsecond-scale matcher, the loop task
never blocks long enough to trip the watchdog.

---

## The generator

`tools/generate_knowledge.py` reads `data/knowledge.json` and emits the C++
tables, precomputing the normalized text, signature, bloom sketch and token
count for every row.

Its normalizer is a byte-for-byte mirror of `AmelTechText::normalize()`. This
is the single most important invariant in the library: if the two disagree,
stored rows never match at runtime and every exact match silently degrades to
fuzzy scoring. That is exactly what happened in v1.

`--selftest` compares the two implementations across the whole corpus and
reports any divergence. Run it before regenerating:

```bash
python3 tools/generate_knowledge.py --selftest
python3 tools/generate_knowledge.py
```

The generator also rejects duplicates after normalization, which is how the two
redundant rows in v1 were found.
