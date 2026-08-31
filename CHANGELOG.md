# Changelog

All notable changes to AmelTech lab's bot.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [semantic versioning](https://semver.org/).

---

## [2.0.0] — 2026-08-31

A full rewrite. The public header is still `AmelTechBot.h` and existing
sketches continue to compile, but almost everything behind it changed.

### Added

**Matching**

- Two-stage matcher. Every row is swept with a precomputed 32-bit token
  signature and a 64-bit character-trigram bloom sketch, which costs two
  popcounts per row. Only the top 12 candidates are scored in full. Typical
  query time dropped from several milliseconds to 25–130 µs.
- Adaptive typo fallback pass, used automatically when the best prefilter score
  is below 0.40, so a badly misspelled question still finds its answer.
- 48-dimensional deterministic embedding (feature hashing with three FNV-1a
  projections, signed, L2-normalized) blended with token overlap, trigram Dice,
  edit similarity and signature overlap.
- Piecewise-linear confidence calibration mapping raw scores onto the STRONG
  (0.90), MODERATE (0.74) and WEAK (0.52) bands.
- Query rewriting. When the first attempt is not confident, the question is
  retried in canonical forms — "tell me about X", "explain X", "define X",
  "info about X" all become "what is X" — and the better result is kept.
- Single-slot query cache, so repeating a question costs almost nothing.

**Sensors**

- Self-contained DHT11 / DHT21 / DHT22 driver. No external library, so the
  behaviour is identical on every ESP32 core.
- Datasheet-correct rate limiting, retries, checksum verification and
  plausibility range checks. A failed read is reported, never fabricated.
- Optional interrupt-safe critical section around the ~4 ms bit-banged read.
- Situation analysis: dew point (Magnus), heat index (Rothfusz), absolute
  humidity, comfort classification, eight-sample trend, and warnings for
  condensation, mould, dry air and conditions bad for the board.
- Situational humour, rotating deterministically through eight banded pools.
  It is only ever appended to a real reading.

**Identity**

- Name and profession memory for up to 34 people, stored in NVS.
- Automatic capture from ordinary chat: "hi my name is Joky Pk", "I'm an
  engineering student", "call me Sam", "this is Priya".
- Boot confirmation. After a reset the first message is answered with
  "Are you \<name\>?", using the most recently seen name.
- Up to four guesses, then "How are you,.. What is your name?".
- Least-recently-seen eviction once 34 names are stored.
- The user's interrupted question is remembered and answered once identity is
  settled.
- Name mention gap, so the bot uses your name periodically rather than in
  every single reply.
- Anti-stuck guards: an unparseable answer never traps the conversation.

**Training**

- Serial console: `train | question | answer`, `train | delete | 0001`,
  `train | delete | full data`, plus `list`, `status`, `save` and `help`.
- Four-digit data numbers, assigned in sequence and preserved across reboots.
- Heap guard. Training is refused while free heap is at or below
  `AMELTECH_TRAIN_MIN_FREE_HEAP` (200 KB), reserving memory for chat logging
  and the matcher. The refusal is explained in plain language.

**Diagnostics**

- Seven weighted components: Memory 26, Stability 20, Thermal 18, Wi-Fi 12,
  CPU 8, Storage 8, Sensors 8.
- Per-component confidence. Anything unmeasured is excluded from the score
  instead of being averaged in, and the report states its own confidence.
- Per-component recommended action, not just a number.
- Continuous scoring curves, so one byte of heap cannot swing the result.
- Smoothed trend, and a worst-component escalation so a single critical
  reading cannot hide behind a good average.

**Thermal**

- `ThermalGuard` with NORMAL / WARM / HOT / CRITICAL states and 4 °C
  hysteresis.
- Automatic CPU throttling to 80 MHz when hot, restored on cooling.
- Cooperative yielding between pipeline stages, with longer pauses when warm.
- Ambient fallback via DHT when no die sensor exists, with the source
  honestly reported.

**Other**

- `tick()` for use in `loop()`: rate-limited telemetry, thermal updates,
  sensor polling and throttled flash writes.
- `pollSerial()` and `handleSerialLine()` for one-line serial integration.
- Heap-aware logging ring buffer that releases itself under memory pressure.
- Four new examples: `DhtTrolling`, `IdentityMemory`, `SerialTraining`,
  `NeuralMatching`.
- Host test suite of 243 checks, plus a CMake target that compile checks every
  example sketch.

### Fixed

Every one of these was verified in the v1.1.0 source before being fixed.

- **Normalizer mismatch.** The runtime normalizer mapped `wifi` to `wi fi` and
  `rssi` to `signal strength`, but `tools/generate_knowledge.py` used a
  different table. Stored normalized text therefore never matched what the
  runtime produced, so exact matches silently degraded to fuzzy scoring on
  every affected row. The two tables are now one specification, and
  `--selftest` proves they agree.
- **Apostrophes split tokens.** "what is ohm's law" normalized to
  `what is ohm s law` while the stored row was `what is ohms law`, so the
  question matched the wrong entry entirely. Apostrophes are now removed
  rather than treated as separators.
- **Flash bloat.** The built-in knowledge array was defined `PROGMEM` inside a
  header, producing one 385 KB copy per translation unit. It is now declared
  in a header and defined exactly once in `knowledge_generated.cpp`.
- **Calculator truncation.** `parse()` silently cut expressions longer than 95
  characters and evaluated the fragment, returning a confident wrong answer.
  Over-long input is now refused.
- **Calculator unreachable.** `looksLikeExpression()` returned false for any
  input containing a letter, so `what is 25*4` never reached the calculator.
- **Calculator over-eager.** The replacement gate accepted any text containing
  a letter, so `what is wifi` was parsed as maths and answered "unknown
  function". Every alphabetic run must now be a name the parser knows.
- **Percent with a symbol.** `15% of 200` returned 15 because only the word
  form `15 percent of 200` was rewritten. Both now give 30.
- **Scientific notation.** `1.5e3` was read as an identifier `e3` and rejected.
  An `e` between a digit and a digit is now correctly treated as an exponent.
- **Dead code in `processIntent()`.** The low-confidence branch built a
  clarification string and then immediately overwrote it, so it never reached
  the user.
- **Placeholder reported as a measurement.** On the classic ESP32,
  `temperatureRead()` returns a constant 53.33 °C. v1 reported it as a live
  reading. It is now detected once and reported `UNSUPPORTED`.
- **Unthrottled Wi-Fi polling.** `WiFi.status()` and `RSSI()` were called on
  every telemetry access. Both are now rate limited.
- **Stale NVS keys.** `saveToNvs()` left orphaned keys behind when the entry
  count shrank, so deleted entries could reappear after a reboot. Stale keys
  are now removed, and writes are deferred and throttled to spare flash.
- **Static RAM waste.** 32 fixed user-knowledge slots occupied about 21.5 KB
  of static RAM whether used or not. Entries are now uniform heap blocks,
  allocated on demand.
- **Always-true guard.** `#if defined(ESP_getMaxAllocHeap) || 1` was tautological
  and hid the fallback path from the compiler.
- **Undefined behaviour.** `isdigit(char)` was called with a possibly negative
  `char` during RSSI parsing.
- Removed an unused `space` variable and several other dead locals.

### Changed

- `Diagnostics::evaluateHealth()` now returns `const HealthReport&` rather than
  a ~1.5 KB struct by value, to keep it off the task stack.
- `MeasurementStatus` gained `MEAS_CACHED` and `MEAS_STALE`, so a value that is
  real but old is distinguishable from one that is current.
- User knowledge capacity raised from 32 to 48 entries.
- Conversation context now stores the matched topic as well as the question,
  which is what makes "tell me more" work.
- The knowledge generator raised `MAX_CATEGORY_LEN` to 32 and added the `funny`
  category.

### Removed

- Two genuine duplicate rows from `data/knowledge.json`: "what is volt"
  (identical to "what is voltage" after normalization) and "what is wi-fi"
  (identical to "what is wifi"). 2039 entries in, 2037 out.
- The `wifi` → `wi fi` and `rssi` → `signal strength` synonym expansions, which
  caused the normalizer mismatch above.

### Migration from 1.1.0

Most sketches need no changes. If you touched internals:

| v1.1.0 | v2.0.0 |
|---|---|
| `HealthReport r = bot.runHealth();` | `const HealthReport& r = bot.diagnostics().evaluateHealth();` |
| `bot.getTelemetry()` fields as plain numbers | fields are `MeasU` / `MeasI` / `MeasF` with a `.value` and a `.status` |
| nothing in `loop()` | call `bot.tick()` |

Taught entries saved by v1.1.0 are not read by v2.0.0; the storage layout
changed to add data numbers. Re-teach them, or export them with
`train | list` before upgrading.

---

## [1.1.0]

### Added

- Conversation context and follow-up handling.
- ESP32 telemetry and diagnostics with a five-component health score.
- Trolling responses.

## [1.0.0]

- First release: knowledge base with exact, keyword and fuzzy matching, a
  four-operator calculator, and NVS persistence.
