# Public API Reference

## Class `AmelTechBot`

### Lifecycle

| Method | Description |
|--------|-------------|
| `bool begin()` | Initialize knowledge, telemetry, diagnostics |
| `void end()` | Release ready state |

### Q&A

| Method | Description |
|--------|-------------|
| `String ask(const String&)` / `ask(const char*)` | Main entry: intent → calculator / hardware / knowledge |

### Knowledge

| Method | Description |
|--------|-------------|
| `AmelTechError train(q, a, category)` | Add user knowledge (alias of addQA) |
| `AmelTechError addQA(q, a, category)` | Add user entry; rejects empty, duplicate, conflict, overflow |
| `AmelTechError removeQA(q)` | Remove user entry |
| `void clearKnowledge()` | Clear **user** knowledge only |
| `size_t getKnowledgeCount()` | Built-in + user |
| `size_t getBuiltinCount()` | Built-in only |
| `size_t getUserCount()` | User only |
| `AmelTechError saveKnowledge()` | Persist user knowledge to NVS |
| `AmelTechError loadKnowledge()` | Load user knowledge from NVS |

### Calculator

| Method | Description |
|--------|-------------|
| `String calculate(expr)` | Evaluate expression; empty string on error |

### Context

| Method | Description |
|--------|-------------|
| `void resetContext()` | Clear conversation context |
| `void setContextSize(uint8_t)` | Bound 0..4 |
| `uint8_t getContextSize()` | Current bound |

### Trolling

| Method | Description |
|--------|-------------|
| `void enableTrolling(bool)` | Optional harmless humor |
| `bool isTrollingEnabled()` | |

### Status

| Method | Description |
|--------|-------------|
| `AmelTechError getLastError()` | Last error code |
| `const char* getLastStatus()` | Human-readable status |
| `float getConfidence()` | Last match confidence |
| `MeasurementStatus getMeasurementStatus()` | Last measurement status |

### Telemetry / diagnostics

| Method | Description |
|--------|-------------|
| `const ESP32Telemetry& getTelemetry(bool full=false)` | Fast or full update |
| `String runDiagnostics(bool full=false)` | Text diagnostic report |
| `String getHealthReport()` | Health score breakdown |

## Error codes

`AMELTECH_OK`, `AMELTECH_INVALID_INPUT`, `AMELTECH_NOT_FOUND`, `AMELTECH_LOW_CONFIDENCE`, `AMELTECH_UNSUPPORTED`, `AMELTECH_UNAVAILABLE`, `AMELTECH_MEASUREMENT_ERROR`, `AMELTECH_MEMORY_ERROR`, `AMELTECH_STORAGE_ERROR`, `AMELTECH_TIMEOUT`, `AMELTECH_INVALID_CONFIGURATION`, `AMELTECH_DUPLICATE`, `AMELTECH_CONFLICT`, `AMELTECH_OVERFLOW`
