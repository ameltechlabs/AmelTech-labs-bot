# Architecture — AmelTechBot

## Design

```
User Sketch
     |
#include <AmelTechBot.h>
     |
AmelTechBot (orchestration, context, trolling, hardware-question routing)
     |
     +-- KnowledgeBase   (normalize/tokenize/match/train, built-in + user knowledge)
     +-- Calculator      (safe arithmetic expression evaluator)
     +-- Telemetry       (structured ESP32 hardware measurements)
     +-- Diagnostics     (health scoring built only from real measurements)
```

`AmelTechBot` is the single public-facing class. It owns one instance of
each subsystem and composes them in `ask()`:

1. **Context resolution** — check if the input references the previous
   answer ("is that good?") using a small bounded ring buffer of recent
   (question, answer, numeric value) tuples.
2. **Calculator detection** — if the trimmed input consists solely of
   digits/operators, route to `Calculator::evaluate()`.
3. **Hardware-question routing** — lightweight keyword-based intent
   detection (`rssi`, `free heap`, `uptime`, `cpu frequency`, `health`,
   `temperature`) routes to a real `Telemetry` sample instead of the
   static knowledge base, so hardware answers are always evidence-based.
4. **Knowledge base query** — normalization → tokenization → exact match →
   keyword/fuzzy/similarity scoring → confidence-gated response.

## Knowledge separation

- **Built-in knowledge** lives in flash (`src/knowledge_generated.h`,
  generated from `data/knowledge.json` by `tools/generate_knowledge.py`).
  It is read-only at runtime — `KnowledgeBase::query()` reads directly from
  the `PROGMEM`-qualified array via `pgm_read_ptr`/`strncpy_P` without
  copying the whole dataset into RAM.
- **User knowledge** lives in a small fixed-size RAM array
  (`AMELTECH_MAX_USER_ENTRIES` = 64 entries), populated via `train()`/
  `addQA()`, and optionally persisted to ESP32 NVS (`Preferences`) via
  `saveKnowledge()`/`loadKnowledge()`.
- `query()` scores against **both** sources and returns whichever has the
  higher confidence, so user training can refine or extend — but never
  silently overwrite — built-in answers (contradictions are rejected at
  `train()` time, not resolved by "last write wins").

## Matching pipeline

1. **Normalize**: lowercase, strip punctuation, collapse whitespace,
   expand a small fixed table of common abbreviations (`r`→`are`,
   `sec`→`seconds`, `min`→`minutes`, contractions), convert digit tokens
   1–10 to their word form, and singularize a small whitelist of unit
   words (`seconds`→`second`, etc.) so numeral/word and singular/plural
   variants align deterministically.
2. **Tokenize**: whitespace split with a small stopword filter (`is`, `a`,
   `the`, `of`, `in`), bounded to `AMELTECH_MAX_TOKENS` (16) tokens.
3. **Score each candidate**:
   - Exact normalized match → confidence 1.0.
   - Otherwise, a weighted blend: `0.5 * keywordCoverage + 0.5 *
     similarity`, where `similarity` is `0.65 * Jaccard(tokens) + 0.35 *
     (1 - normalizedLevenshtein)`, bounded and capped below 1.0 (exact
     match is reserved for score 1.0).
4. **Confidence gating** in `AmelTechBot::ask()`: ≥0.90 strong, 0.75–0.89
   moderate, 0.50–0.74 clarification, <0.50 explicit "don't know."

This is a **lightweight, deterministic, non-neural** similarity model —
documented explicitly as such, per the requirement not to claim semantic
understanding beyond what is actually implemented.

## Memory & performance discipline

- Fixed-size char buffers for all stored questions/answers/categories
  (`AMELTECH_MAX_QUESTION_LEN`=96, `AMELTECH_MAX_ANSWER_LEN`=220,
  `AMELTECH_MAX_CATEGORY_LEN`=24).
- Bounded user-knowledge table (64 entries) and bounded context ring
  buffer (≤8 entries) — no unbounded growth from user input.
- Levenshtein distance uses a two-row DP with an early-exit bound and a
  hard 128-character cap, so fuzzy matching cost is bounded regardless of
  input length.
- Built-in knowledge is read from flash on demand, not duplicated into
  RAM at `begin()`.
- Two-speed telemetry keeps expensive operations (I2C scan, flash/PSRAM
  queries) out of the default fast path.

## Extensibility

New ESP32 families: add a `CONFIG_IDF_TARGET_*` branch in
`Telemetry::detectFamily()` and a GPIO reference-count case in
`_fillGpioSlow()`. New telemetry fields: add to the relevant sub-struct in
`Telemetry.h` and a corresponding fill method in `Telemetry.cpp`, always
gated behind a capability check with an explicit non-`LIVE` status when
unsupported. New knowledge: edit `data/knowledge.json` and re-run
`tools/generate_knowledge.py` — never hand-edit
`src/knowledge_generated.h`.
