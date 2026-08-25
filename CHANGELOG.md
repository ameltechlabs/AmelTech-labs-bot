# Changelog

All notable changes to AmelTech lab's bot will be documented in this file.

## [1.1.0] - 2026-08-24

### Added
- Expanded built-in knowledge base from 276 to 2039 entries, covering general
  knowledge, ESP32/Arduino projects, sensors, chemistry, space, disasters/hazards,
  cybersecurity, AI usage, cricket player profiles, and more
- `tools/generate_knowledge.py`: extended `VALID_CATEGORIES` to cover the new
  category set (`gk`, `sensor`, `mock`, `space_gk`, `earth_gk`, etc.)

### Changed
- `tools/generate_knowledge.py`: raised `MAX_ENTRIES` sanity ceiling from 512 to
  4096 to accommodate the larger knowledge base (~300KB of flash strings —
  well within typical ESP32 flash budgets)
- `tools/generate_knowledge.py`: `escape_c_string()` now correctly encodes
  non-ASCII characters as UTF-8 byte sequences (one `\xNN` escape per byte,
  with literal-string splitting to avoid hex-digit run-on). Previously,
  characters above U+00FF (e.g. em dashes) were emitted as a single invalid
  `\xNNNN` escape, which is undefined behavior in a narrow C string literal.
  This also fixes existing entries containing accented names and symbols
  (e.g. "Brasília", "₹", "Mbappé").

### Fixed
- Merged and deduplicated the expanded training data against the existing
  knowledge base: 2451 combined entries reduced to 2039 after removing exact
  duplicates and resolving 357 conflicting/duplicate questions (preferring
  specific categories such as `science`/`electronics` over the generic `gk`
  bucket when the same question appeared in both)
- Repaired one malformed entry in the source training data (missing opening
  quote on an answer string)
- Rewrote roughly 70 answers in the `mock` and `funny_trolling` categories
  that previously contained slurs, insults directed at the user, or crude
  sexual content. Replacements keep the same light, sarcastic tone but
  respond respectfully and decline inappropriate requests plainly instead

## [1.0.0] - 2026-08-21

### Added
- Complete offline knowledge engine with exact, normalized, keyword, fuzzy and lightweight similarity matching
- Built-in knowledge base generated from `data/knowledge.json` into flash-friendly `knowledge_generated.h`
- User training API with duplicate and contradiction detection
- NVS persistence for user knowledge (ESP32 Preferences)
- Safe expression calculator (`+ - * / %`, parentheses, decimals, precedence)
- Bounded conversation context for simple follow-ups
- ESP32 telemetry model with explicit measurement statuses (LIVE, CACHED, STALE, UNAVAILABLE, UNSUPPORTED, ERROR)
- Diagnostics and explainable health scoring
- Optional harmless trolling mode
- Public API via single header `AmelTechBot.h`
- Examples: BasicChat, TrainKnowledge, GeneralKnowledge, Calculator, ESP32Diagnostics, WiFiDiagnostics, MemoryDiagnostics, PerformanceMonitor, SmartHardwareChat, FullDemo
- Host-side test scaffold
- Documentation: README, API, TELEMETRY, ARCHITECTURE, STATUS

### Notes
- Hardware measurements are never fabricated
- Unsupported capabilities return explicit UNSUPPORTED / UNAVAILABLE
- ESP32 family differences are respected via compile-time checks where practical
