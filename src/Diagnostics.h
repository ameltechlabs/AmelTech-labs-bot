/*
 * Diagnostics.h
 * ---------------------------------------------------------------------------
 * Evidence-weighted health scoring.
 *
 * What changed from the previous release, and why it is more accurate:
 *
 *   1. Components are weighted. A low heap matters far more than an idle
 *      Wi-Fi radio, so memory carries 26% of the score and Wi-Fi 12%.
 *   2. Unmeasured components no longer drag the score down. Previously an
 *      UNKNOWN component scored a flat 50 and was averaged in, so a board with
 *      no temperature sensor looked half broken. Now each component reports a
 *      confidence, and the overall score is the confidence-weighted mean of
 *      what was actually measured. The report states its own confidence.
 *   3. Every component reports a recommended action, not just a number.
 *   4. Scores move smoothly. Free heap is scored on a continuous curve rather
 *      than four hard steps, so one byte cannot swing the result by 25 points.
 *   5. The overall trend is tracked, so "getting worse" is visible.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_DIAGNOSTICS_H
#define AMELTECH_DIAGNOSTICS_H

#include <Arduino.h>
#include "AmelTechConfig.h"
#include "Telemetry.h"
#include "ThermalGuard.h"
#include "SensorHub.h"

enum HealthLevel : uint8_t {
    HEALTH_NORMAL = 0,
    HEALTH_INFO,
    HEALTH_WARNING,
    HEALTH_HIGH,
    HEALTH_CRITICAL,
    HEALTH_UNKNOWN
};

#define AMELTECH_HEALTH_COMPONENTS 7

struct HealthComponent {
    const char* name;
    uint8_t score;       // 0-100
    uint8_t confidence;  // 0-100; 0 means "not measured, excluded from score"
    uint8_t weight;      // relative importance
    HealthLevel level;
    char detail[64];
    char action[80];
};

struct HealthReport {
    int overallScore;          // 0-100, confidence weighted
    uint8_t overallConfidence; // 0-100, share of weight actually measured
    HealthLevel overallLevel;
    HealthComponent components[AMELTECH_HEALTH_COMPONENTS];
    uint8_t componentCount;
    int8_t trendDelta;         // change against the smoothed previous score
    char mainIssue[160];
    char summary[224];
};

class Diagnostics {
public:
    Diagnostics();

    void begin(Telemetry* telemetry,
               ThermalGuard* thermal = nullptr,
               SensorHub* sensors = nullptr);

    // Refreshes telemetry and returns the human readable report.
    String run(bool full = false);

    // Recomputes from whatever telemetry currently holds. Returned by
    // reference: the report is ~1.5 KB and copying it onto a task stack for
    // every call would be wasteful.
    const HealthReport& evaluateHealth();
    const HealthReport& lastReport() const { return _report; }

    String healthReportString();
    int healthScore();

    static const char* levelToString(HealthLevel l);

private:
    Telemetry* _telem;
    ThermalGuard* _thermal;
    SensorHub* _sensors;
    bool _begun;
    HealthReport _report;
    int _emaScore;
    bool _hasEma;

    void scoreCpu(HealthComponent& c, const ESP32Telemetry& t);
    void scoreMemory(HealthComponent& c, const ESP32Telemetry& t);
    void scoreThermal(HealthComponent& c, const ESP32Telemetry& t);
    void scoreWifi(HealthComponent& c, const ESP32Telemetry& t);
    void scoreStorage(HealthComponent& c, const ESP32Telemetry& t);
    void scoreSensors(HealthComponent& c, const ESP32Telemetry& t);
    void scoreStability(HealthComponent& c, const ESP32Telemetry& t);

    static void initComponent(HealthComponent& c, const char* name, uint8_t weight);
    static HealthLevel levelForScore(uint8_t score);
    static uint8_t curveScore(float value, float best, float worst);
};

#endif // AMELTECH_DIAGNOSTICS_H
