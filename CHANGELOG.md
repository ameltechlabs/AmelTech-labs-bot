# Changelog

All notable changes to AmelTech lab's bot are documented in this file.

## [1.0.0] - 2026-08-21

### Added
- Initial release of `AmelTechBot`, a single-header-facing ESP32 Arduino library.
- Offline question-answer engine: normalization, tokenization, exact matching,
  keyword matching, fuzzy (bounded Levenshtein) matching, and a lightweight
  token-overlap + edit-distance "semantic-style" similarity blend.
- Built-in knowledge base (30 entries across science/math/esp32/general
  categories) compiled into flash via `tools/generate_knowledge.py` and
  `src/knowledge_generated.h`. No user training required for built-in Q&A.
- User-trainable knowledge (`train()`/`addQA()`/`removeQA()`/`clearKnowledge()`)
  with duplicate and contradiction detection, kept separate from built-in
  knowledge, and persistable to ESP32 NVS via `saveKnowledge()`/`loadKnowledge()`.
- Confidence-scored answers with four tiers (strong/moderate/clarify/unknown)
  and an explicit "I don't have enough reliable information" fallback —
  never a fabricated answer.
- Safe embedded calculator (`+ - * / % ( )`, decimals, percentage handling,
  operator precedence) with explicit error codes for division-by-zero,
  malformed input, invalid characters, and non-finite results.
- Bounded conversational context (default 4, max 8 entries) supporting
  simple pronoun/deictic follow-ups such as "is that good?".
- ESP32 family detection (ESP32, S2, S3, C3, C6, H2) via compile-time
  target macros.
- Structured telemetry model (`ESP32Telemetry`) covering chip, CPU, memory,
  Wi-Fi, Bluetooth, UART, I2C, SPI, GPIO, ADC, DAC, PWM, temperature,
  watchdog, and system fields, each with an explicit measurement status
  (`LIVE`/`CACHED`/`STALE`/`UNAVAILABLE`/`UNSUPPORTED`/`MEASUREMENT_ERROR`).
  Zero is never used to mean "unsupported."
- Two-speed telemetry: `getTelemetry(false)` for cheap/fast fields,
  `getTelemetry(true)` for a full scan including flash/PSRAM/I2C-scan/GPIO
  inventory.
- Explainable health scoring engine (`getHealthReport()`/`runDiagnostics()`)
  that only scores subsystems with actual measurements and reports
  `mainIssue` from the worst measured subsystem.
- Optional, harmless "trolling mode" (`enableTrolling()`) that appends
  secondary commentary after a real answer; never replaces warnings.
- Full error/status enum (`AmelTechStatus`) exposed through the public API.
- Ten example sketches covering chat, training, calculator, diagnostics,
  Wi-Fi/memory telemetry, performance monitoring, hardware-aware chat, and
  a full end-to-end demo.
- Host-side unit test suite (`tests/host_test.cpp`, g++/CMake-buildable)
  covering normalization, matching, training/contradiction detection,
  confidence thresholds, and calculator behavior — 34/34 passing.
- Documentation: `README.md`, `docs/API.md`, `docs/TELEMETRY.md`,
  `docs/ARCHITECTURE.md`, `docs/STATUS.md`.

### Known limitations
- ESP32 hardware compilation was not verified in this environment (no
  Arduino CLI / ESP32 core toolchain available). See `docs/STATUS.md`.
- Similarity matching is a lightweight token-overlap + edit-distance blend,
  not a trained embedding/neural model.
