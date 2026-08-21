# Changelog

All notable changes to AmelTech lab's bot will be documented in this file.

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
