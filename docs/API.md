# API Reference — AmelTechBot

Single include: `#include <AmelTechBot.h>`

## Class: `AmelTechBot`

### Lifecycle

| Method | Description |
|---|---|
| `AmelTechStatus begin()` | Initializes built-in knowledge, telemetry, diagnostics; loads persisted user knowledge from NVS if available. Call once in `setup()`. |

### Conversation

| Method | Description |
|---|---|
| `String ask(const String& input)` | Full pipeline: normalize → calculator detection → hardware-question routing → knowledge lookup with confidence thresholds. Never fabricates an answer below 0.50 confidence. |
| `void resetContext()` | Clears bounded conversational context. |
| `AmelTechStatus setContextSize(uint8_t size)` | Sets context capacity (clamped to `AMELTECH_MAX_CONTEXT_SIZE` = 8); resets context. |
| `uint8_t getContextSize()` | Returns current configured context capacity. |

### Training / knowledge management

| Method | Description |
|---|---|
| `AmelTechStatus train(question, answer, category="custom")` | Adds user knowledge with validation, duplicate, and contradiction checks. |
| `AmelTechStatus addQA(question, answer, category="custom")` | Alias for `train()`. |
| `AmelTechStatus removeQA(question)` | Removes a user-trained entry by (normalized) question. Returns `AMELTECH_NOT_FOUND` if absent. |
| `AmelTechStatus clearKnowledge()` | Clears all **user** knowledge (built-in knowledge is untouched). |
| `size_t getKnowledgeCount()` | Built-in + user entry count. |
| `AmelTechStatus saveKnowledge()` | Persists user knowledge to ESP32 NVS (`Preferences`). Returns `AMELTECH_UNSUPPORTED` on non-ESP32 builds. |
| `AmelTechStatus loadKnowledge()` | Loads user knowledge from NVS. |

### Trolling mode

| Method | Description |
|---|---|
| `void enableTrolling(bool)` | Enables/disables optional harmless commentary appended after real answers. |
| `bool isTrollingEnabled()` | Current state. |

### Status / confidence introspection

| Method | Description |
|---|---|
| `AmelTechStatus getLastError()` | Error from the most recent operation. |
| `AmelTechStatus getLastStatus()` | Status from the most recent `ask()`/`train()`/`calculate()` call. |
| `float getConfidence()` | 0.0–1.0 confidence of the last `ask()` match. |
| `AmelTechConfidenceTier getConfidenceTier()` | Bucketed tier of the above. |
| `AmelTechMeasurementStatus getMeasurementStatus()` | Measurement status backing the last hardware-derived answer. |

### Telemetry / diagnostics / health

| Method | Description |
|---|---|
| `ESP32Telemetry getTelemetry(bool fullScan=false)` | Fast (cheap) or full (slow, includes flash/PSRAM/I2C scan/GPIO inventory) snapshot. |
| `DiagnosticsReport runDiagnostics(bool fullScan=false)` | Telemetry snapshot + health report + human-readable summary. |
| `HealthReport getHealthReport()` | Health report from a fast telemetry sample. |

### Calculator

| Method | Description |
|---|---|
| `CalcResult calculate(const String& expression)` | Evaluates a bounded arithmetic expression. See `Calculator.h`. |

## Error / Status Codes (`AmelTechStatus`)

`AMELTECH_OK`, `AMELTECH_INVALID_INPUT`, `AMELTECH_NOT_FOUND`,
`AMELTECH_LOW_CONFIDENCE`, `AMELTECH_UNSUPPORTED`, `AMELTECH_UNAVAILABLE`,
`AMELTECH_MEASUREMENT_ERROR`, `AMELTECH_MEMORY_ERROR`, `AMELTECH_STORAGE_ERROR`,
`AMELTECH_TIMEOUT`, `AMELTECH_INVALID_CONFIGURATION`, `AMELTECH_DUPLICATE`,
`AMELTECH_CONTRADICTION`

Use `ameltechStatusToString(status)` for a human-readable string.

## Confidence Tiers

| Tier | Range | Behavior |
|---|---|---|
| `AMELTECH_CONF_STRONG` | ≥ 0.90 | Direct answer |
| `AMELTECH_CONF_MODERATE` | 0.75–0.89 | Direct answer |
| `AMELTECH_CONF_CLARIFY` | 0.50–0.74 | Clarification-style response with the best-guess matched question |
| `AMELTECH_CONF_UNKNOWN` | < 0.50 | "I don't have enough reliable information to answer that." |

## Calculator (`Calculator.h`)

```cpp
struct CalcResult {
    CalcStatus status;
    double value;
    bool valid;
    String message;
};
```

Supported: `+ - * / % ( )`, decimals, unary +/-, percentage suffix (`50%` → 0.5),
operator precedence. Max expression length: 64 characters. Max parenthesis
nesting: 12. Errors: `CALC_ERROR_SYNTAX`, `CALC_ERROR_DIV_BY_ZERO`,
`CALC_ERROR_INVALID_CHAR`, `CALC_ERROR_NON_FINITE`, `CALC_ERROR_TOO_LONG`,
`CALC_ERROR_EMPTY`.

## Telemetry & Diagnostics

See `docs/TELEMETRY.md` for the full field list and measurement-status
semantics, and `docs/ARCHITECTURE.md` for how modules fit together.
