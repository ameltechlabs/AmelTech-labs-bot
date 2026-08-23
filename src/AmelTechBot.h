// =============================================================
// AmelTechBot.h
//
// AmelTech lab's bot — offline-capable ESP32 knowledge/telemetry
// assistant library.
//
// This is the single public header most sketches need:
//     #include <AmelTechBot.h>
//
// See README.md and docs/API.md for full documentation.
// =============================================================
#ifndef AMELTECH_BOT_H
#define AMELTECH_BOT_H

#include <Arduino.h>
#include "AmelTechTypes.h"
#include "KnowledgeBase.h"
#include "Calculator.h"
#include "Telemetry.h"
#include "Diagnostics.h"

// -------------------------------------------------------------
// Limits (must match tools/generate_knowledge.py)
// -------------------------------------------------------------
#define AMELTECH_MAX_QUESTION_LEN   96
#define AMELTECH_MAX_ANSWER_LEN     220
// 34, not 24: the longest name in generate_knowledge.py's VALID_CATEGORIES
// ("football_player_celebrity_profile") is 33 chars. 24 was too small for
// the whitelist itself, so no dataset using that category could ever pass
// validation. Raised to 34 (33 chars + null terminator headroom).
#define AMELTECH_MAX_CATEGORY_LEN   34
#define AMELTECH_MAX_USER_ENTRIES   64
#define AMELTECH_MAX_CONTEXT_SIZE   8
#define AMELTECH_DEFAULT_CONTEXT_SIZE 4
#define AMELTECH_MAX_TOKENS         16

// -------------------------------------------------------------
// Trolling mode budget: purely cosmetic, appended after a real
// answer, never replaces a warning/critical message.
// -------------------------------------------------------------

class AmelTechBot {
public:
    AmelTechBot();

    // ---------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------
    // Initializes built-in knowledge (from flash), loads any
    // persisted user knowledge from NVS (if available), and
    // prepares telemetry subsystems. Safe to call once in setup().
    AmelTechStatus begin();

    // ---------------------------------------------------------
    // Conversational interface
    // ---------------------------------------------------------
    // Ask a natural-language question. Internally normalizes input,
    // checks for calculator expressions, checks context, and
    // queries the knowledge engine. Never fabricates an answer for
    // low-confidence matches; returns a clarification message
    // instead. Use getConfidence()/getLastStatus() after calling
    // for details about how the answer was produced.
    String ask(const String& input);

    // ---------------------------------------------------------
    // Training / knowledge management (user knowledge only —
    // built-in knowledge is supplied by the library and is
    // read-only at runtime)
    // ---------------------------------------------------------
    AmelTechStatus train(const String& question, const String& answer, const String& category = "custom");
    AmelTechStatus addQA(const String& question, const String& answer, const String& category = "custom");
    AmelTechStatus removeQA(const String& question);
    AmelTechStatus clearKnowledge();
    size_t getKnowledgeCount() const;

    // Persistence (ESP32 Preferences/NVS) for user-trained knowledge only.
    AmelTechStatus saveKnowledge();
    AmelTechStatus loadKnowledge();

    // ---------------------------------------------------------
    // Trolling mode (optional, harmless, secondary commentary)
    // ---------------------------------------------------------
    void enableTrolling(bool enabled);
    bool isTrollingEnabled() const;

    // ---------------------------------------------------------
    // Status / confidence introspection (reflects the most
    // recent ask()/train()/calculate() call)
    // ---------------------------------------------------------
    AmelTechStatus getLastError() const;
    AmelTechStatus getLastStatus() const;
    float getConfidence() const;               // 0.0 - 1.0
    AmelTechConfidenceTier getConfidenceTier() const;

    // Reports whether the last measurement-backed answer (e.g. a
    // telemetry-derived response) was LIVE/CACHED/STALE/etc.
    AmelTechMeasurementStatus getMeasurementStatus() const;

    // ---------------------------------------------------------
    // Telemetry / diagnostics / health (see Telemetry.h, Diagnostics.h)
    // ---------------------------------------------------------
    ESP32Telemetry getTelemetry(bool fullScan = false);
    DiagnosticsReport runDiagnostics(bool fullScan = false);
    HealthReport getHealthReport();

    // ---------------------------------------------------------
    // Calculator
    // ---------------------------------------------------------
    CalcResult calculate(const String& expression);

    // ---------------------------------------------------------
    // Context memory (bounded)
    // ---------------------------------------------------------
    void resetContext();
    AmelTechStatus setContextSize(uint8_t size); // clamped to AMELTECH_MAX_CONTEXT_SIZE
    uint8_t getContextSize() const;

private:
    KnowledgeBase _knowledge;
    Calculator _calculator;
    Telemetry _telemetry;
    Diagnostics _diagnostics;

    bool _began;
    bool _trollingEnabled;

    AmelTechStatus _lastError;
    AmelTechStatus _lastStatus;
    float _lastConfidence;
    AmelTechMeasurementStatus _lastMeasurementStatus;

    // Bounded conversational context: stores last N (question, answer,
    // category, numeric-if-any) tuples for simple pronoun resolution
    // ("is that good?", "what about that?").
    struct ContextEntry {
        String question;
        String answer;
        String category;
        bool hasNumeric;
        float numericValue;
        String numericUnit;
    };
    ContextEntry _context[AMELTECH_MAX_CONTEXT_SIZE];
    uint8_t _contextSize;      // configured capacity (<= AMELTECH_MAX_CONTEXT_SIZE)
    uint8_t _contextCount;     // number of valid entries currently stored
    uint8_t _contextHead;      // ring-buffer head index

    void _pushContext(const String& question, const String& answer, const String& category,
                       bool hasNumeric, float numericValue, const String& numericUnit);
    bool _resolveContextReference(const String& normalizedInput, String& outResolved) const;
    String _applyTrolling(const String& baseAnswer, const String& category) const;
    String _handleHardwareQuestion(const String& normalizedInput, bool& handled);
};

#endif // AMELTECH_BOT_H
