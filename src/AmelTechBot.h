/*
 * AmelTech lab's bot
 * Offline-capable ESP32 Arduino library
 * Knowledge engine + Calculator + Telemetry + Diagnostics + Health
 *
 * Public header — user sketches normally need only:
 *   #include <AmelTechBot.h>
 */

#ifndef AMELTECH_BOT_H
#define AMELTECH_BOT_H

#include <Arduino.h>
#include "KnowledgeBase.h"
#include "Calculator.h"
#include "Telemetry.h"
#include "Diagnostics.h"

// ---------------------------------------------------------------------------
// Error / status codes
// ---------------------------------------------------------------------------
enum AmelTechError : int8_t {
    AMELTECH_OK = 0,
    AMELTECH_INVALID_INPUT = -1,
    AMELTECH_NOT_FOUND = -2,
    AMELTECH_LOW_CONFIDENCE = -3,
    AMELTECH_UNSUPPORTED = -4,
    AMELTECH_UNAVAILABLE = -5,
    AMELTECH_MEASUREMENT_ERROR = -6,
    AMELTECH_MEMORY_ERROR = -7,
    AMELTECH_STORAGE_ERROR = -8,
    AMELTECH_TIMEOUT = -9,
    AMELTECH_INVALID_CONFIGURATION = -10,
    AMELTECH_DUPLICATE = -11,
    AMELTECH_CONFLICT = -12,
    AMELTECH_OVERFLOW = -13
};

// ---------------------------------------------------------------------------
// Confidence bands
// ---------------------------------------------------------------------------
// >= 0.90  Strong
// 0.75-0.89 Moderate
// 0.50-0.74 Clarification preferred
// < 0.50   Unknown

class AmelTechBot {
public:
    AmelTechBot();
    ~AmelTechBot();

    // Lifecycle
    bool begin();
    void end();

    // Primary Q&A
    String ask(const String& question);
    String ask(const char* question);

    // Knowledge management (user knowledge; built-in is separate)
    AmelTechError train(const String& question, const String& answer, const String& category = "custom");
    AmelTechError addQA(const String& question, const String& answer, const String& category = "custom");
    AmelTechError removeQA(const String& question);
    void clearKnowledge();  // clears user knowledge only
    size_t getKnowledgeCount() const;  // built-in + user
    size_t getBuiltinCount() const;
    size_t getUserCount() const;

    // Persistence (user knowledge via Preferences/NVS)
    AmelTechError saveKnowledge();
    AmelTechError loadKnowledge();

    // Calculator
    String calculate(const String& expression);
    String calculate(const char* expression);

    // Context
    void resetContext();
    void setContextSize(uint8_t size);
    uint8_t getContextSize() const;

    // Trolling (harmless optional humor)
    void enableTrolling(bool enable);
    bool isTrollingEnabled() const;

    // Status / confidence
    AmelTechError getLastError() const;
    const char* getLastStatus() const;
    float getConfidence() const;
    MeasurementStatus getMeasurementStatus() const;

    // Telemetry & diagnostics
    const ESP32Telemetry& getTelemetry(bool full = false);
    String runDiagnostics(bool full = false);
    String getHealthReport();

    // Introspection
    bool isReady() const { return _ready; }

private:
    KnowledgeBase _kb;
    Calculator _calc;
    Telemetry _telem;
    Diagnostics _diag;

    bool _ready;
    bool _trolling;
    AmelTechError _lastError;
    float _lastConfidence;
    MeasurementStatus _lastMeasStatus;
    char _lastStatusBuf[96];

    // Bounded context
    static const uint8_t MAX_CONTEXT = 4;
    uint8_t _contextSize;
    struct ContextItem {
        char question[96];
        char answer[160];
        char topic[32];
        bool valid;
    };
    ContextItem _context[MAX_CONTEXT];
    uint8_t _contextHead;

    // Internal helpers
    void setError(AmelTechError err, const char* status);
    String processIntent(const String& normalized, const String& original);
    String handleHardwareQuery(const String& normalized);
    String handleFollowUp(const String& normalized);
    void pushContext(const char* q, const char* a, const char* topic);
    String maybeTroll(const String& answer, const String& category);
    bool looksLikeMath(const String& s) const;
    String normalizeInput(const String& s) const;
};

#endif // AMELTECH_BOT_H
