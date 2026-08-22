// =============================================================
// AmelTechTypes.h
//
// Shared lightweight enums used across modules (KnowledgeBase,
// Calculator, AmelTechBot). Kept dependency-free (no Arduino.h,
// no ESP32-only headers) so that host-side unit tests can compile
// KnowledgeBase.cpp/Calculator.cpp without pulling in Telemetry.h
// or Diagnostics.h, which depend on ESP32-only APIs.
// =============================================================
#ifndef AMELTECH_TYPES_H
#define AMELTECH_TYPES_H

// -------------------------------------------------------------
// Error / status codes (see item 31 of the spec)
// -------------------------------------------------------------
enum AmelTechStatus {
    AMELTECH_OK = 0,
    AMELTECH_INVALID_INPUT,
    AMELTECH_NOT_FOUND,
    AMELTECH_LOW_CONFIDENCE,
    AMELTECH_UNSUPPORTED,
    AMELTECH_UNAVAILABLE,
    AMELTECH_MEASUREMENT_ERROR,
    AMELTECH_MEMORY_ERROR,
    AMELTECH_STORAGE_ERROR,
    AMELTECH_TIMEOUT,
    AMELTECH_INVALID_CONFIGURATION,
    AMELTECH_DUPLICATE,
    AMELTECH_CONTRADICTION
};

// Human-readable helper (defined in AmelTechBot.cpp)
const char* ameltechStatusToString(AmelTechStatus status);

// -------------------------------------------------------------
// Confidence tiers (see item 7)
// -------------------------------------------------------------
enum AmelTechConfidenceTier {
    AMELTECH_CONF_STRONG = 0,   // >= 0.90
    AMELTECH_CONF_MODERATE,     // 0.75 - 0.89
    AMELTECH_CONF_CLARIFY,      // 0.50 - 0.74
    AMELTECH_CONF_UNKNOWN       // <  0.50
};

#endif // AMELTECH_TYPES_H
