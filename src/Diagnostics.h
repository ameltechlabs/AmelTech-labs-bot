/*
 * Diagnostics & Health engine
 * Evidence-based scores only. Never fabricate.
 */

#ifndef AMELTECH_DIAGNOSTICS_H
#define AMELTECH_DIAGNOSTICS_H

#include <Arduino.h>
#include "Telemetry.h"

enum HealthLevel : uint8_t {
    HEALTH_NORMAL = 0,
    HEALTH_INFO,
    HEALTH_WARNING,
    HEALTH_HIGH,
    HEALTH_CRITICAL,
    HEALTH_UNKNOWN
};

struct HealthComponent {
    const char* name;
    int score;          // 0-100
    HealthLevel level;
    char detail[64];
};

struct HealthReport {
    int overallScore;   // 0-100
    HealthLevel overallLevel;
    HealthComponent components[6];
    int componentCount;
    char mainIssue[96];
    char summary[160];
};

class Diagnostics {
public:
    Diagnostics();

    void begin(Telemetry* telem);
    String run(bool full = false);
    HealthReport evaluateHealth();
    String healthReportString();

    static const char* levelToString(HealthLevel l);

private:
    Telemetry* _telem;
    bool _begun;

    void scoreCpu(HealthComponent& c, const ESP32Telemetry& t);
    void scoreMemory(HealthComponent& c, const ESP32Telemetry& t);
    void scoreWifi(HealthComponent& c, const ESP32Telemetry& t);
    void scoreCommunication(HealthComponent& c, const ESP32Telemetry& t);
    void scoreSystem(HealthComponent& c, const ESP32Telemetry& t);
};

#endif // AMELTECH_DIAGNOSTICS_H
