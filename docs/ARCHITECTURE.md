# Architecture

```
User Sketch
     ↓
#include <AmelTechBot.h>
     ↓
AmelTechBot
     ├── Knowledge Engine (KnowledgeBase)
     │     ├── Built-in (PROGMEM / knowledge_generated.h)
     │     └── User (RAM + optional NVS)
     ├── Calculator
     ├── Context (bounded ring)
     ├── Confidence / matching
     ├── Telemetry
     ├── Diagnostics / Health
     ├── Persistence (Preferences)
     └── Response Engine (intent routing)
```

## Intent routing (ask)

1. Empty / oversized → error
2. Looks like math → Calculator
3. Hardware keywords → Telemetry-backed answers
4. Pronoun / follow-up → Context
5. KnowledgeBase findBest → confidence bands
6. Optional trolling append (never replaces warnings)

## Matching pipeline

Normalize (case, punctuation, whitespace, abbreviations) →  
Exact / normalized → Keyword overlap → Fuzzy (bounded Levenshtein) → Lightweight blend → Confidence

## Memory strategy

- Fixed-size user entry array
- PROGMEM built-in table
- Bounded context slots
- No unbounded loops on user input
- Prefer stack / static buffers in parsers

## Extensibility

New ESP32 variants: extend compile-time checks in `Telemetry.cpp`.  
New knowledge: edit `data/knowledge.json` and regenerate.
