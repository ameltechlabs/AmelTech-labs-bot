// =============================================================
// Diagnostics.h
//
// System diagnostics + explainable health scoring engine, built
// entirely from actual Telemetry measurements. Never fabricates
// a score for a subsystem that was not actually measured.
// =============================================================
#ifndef AMELTECH_DIAGNOSTICS_H
#define AMELTECH_DIAGNOSTICS_H

#include <Arduino.h>
#include "Telemetry.h"

enum HealthLevel {
    HEALTH_NORMAL = 0,
    HEALTH_INFO,
    HEALTH_WARNING,
    HEALTH_HIGH,
    HEALTH_CRITICAL,
    HEALTH_UNKNOWN
};

const char* healthLevelToString(HealthLevel level);

struct SubsystemHealth {
    String name;
    HealthLevel level;
    String note;
    bool wasMeasured;
};

struct HealthReport {
    uint8_t overallScore;      // 0-100, only computed from measured subsystems
    HealthLevel overallLevel;
    static const uint8_t MAX_SUBSYSTEMS = 8;
    SubsystemHealth subsystems[MAX_SUBSYSTEMS];
    uint8_t subsystemCount;
    String mainIssue;          // empty if none found
    unsigned long generatedAtMs;
};

struct DiagnosticsReport {
    ESP32Telemetry telemetry;
    HealthReport health;
    bool wasFullScan;
    String summary;
};

class Diagnostics {
public:
    Diagnostics();
    void begin();

    // Builds a diagnostics report from a telemetry snapshot.
    DiagnosticsReport buildReport(const ESP32Telemetry& telemetry, bool wasFullScan) const;

    // Builds just the health report/score from a telemetry snapshot.
    HealthReport buildHealthReport(const ESP32Telemetry& telemetry) const;

private:
    SubsystemHealth _evaluateCpu(const ESP32Telemetry& t) const;
    SubsystemHealth _evaluateMemory(const ESP32Telemetry& t) const;
    SubsystemHealth _evaluateWiFi(const ESP32Telemetry& t) const;
    SubsystemHealth _evaluateCommunication(const ESP32Telemetry& t) const;
    SubsystemHealth _evaluateSystem(const ESP32Telemetry& t) const;
};

#endif // AMELTECH_DIAGNOSTICS_H
