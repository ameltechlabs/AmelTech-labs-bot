// =============================================================
// Diagnostics.cpp
//
// Health scoring rules (documented, deterministic):
//   Each subsystem that has at least one LIVE/CACHED measurement
//   contributes a 0-100 sub-score based on documented thresholds.
//   Subsystems with no measured data are marked UNKNOWN and are
//   EXCLUDED from the overall score average (never penalized or
//   fabricated).
//   Overall score = average of measured subsystem scores.
// =============================================================
#include "Diagnostics.h"

const char* healthLevelToString(HealthLevel level) {
    switch (level) {
        case HEALTH_NORMAL: return "NORMAL";
        case HEALTH_INFO: return "INFO";
        case HEALTH_WARNING: return "WARNING";
        case HEALTH_HIGH: return "HIGH";
        case HEALTH_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

Diagnostics::Diagnostics() {}
void Diagnostics::begin() {}

static HealthLevel scoreToLevel(int score) {
    if (score >= 90) return HEALTH_NORMAL;
    if (score >= 75) return HEALTH_INFO;
    if (score >= 50) return HEALTH_WARNING;
    if (score >= 25) return HEALTH_HIGH;
    return HEALTH_CRITICAL;
}

SubsystemHealth Diagnostics::_evaluateCpu(const ESP32Telemetry& t) const {
    SubsystemHealth s;
    s.name = "CPU";
    s.wasMeasured = false;
    s.level = HEALTH_UNKNOWN;
    s.note = "No CPU timing samples recorded.";

    if (t.cpu.frequencyMHz.status == MEAS_LIVE) {
        s.wasMeasured = true;
        s.level = HEALTH_NORMAL;
        s.note = "CPU frequency nominal.";
    }

    if (t.cpu.jitterUs.status == MEAS_LIVE) {
        s.wasMeasured = true;
        float jitter = t.cpu.jitterUs.value;
        float avg = (t.cpu.avgLoopTimeUs.status == MEAS_LIVE) ? t.cpu.avgLoopTimeUs.value : 0.0f;
        if (avg > 0 && jitter > (avg * 2.0f)) {
            s.level = HEALTH_WARNING;
            s.note = "High loop-timing jitter detected.";
        } else {
            if (s.level != HEALTH_WARNING) s.level = HEALTH_NORMAL;
        }
    }

    return s;
}

SubsystemHealth Diagnostics::_evaluateMemory(const ESP32Telemetry& t) const {
    SubsystemHealth s;
    s.name = "Memory";
    s.wasMeasured = false;
    s.level = HEALTH_UNKNOWN;
    s.note = "Heap not measured.";

    if (t.memory.freeHeapBytes.status == MEAS_LIVE) {
        s.wasMeasured = true;
        uint32_t freeHeap = t.memory.freeHeapBytes.value;
        if (freeHeap < 10000) {
            s.level = HEALTH_CRITICAL;
            s.note = "Free heap critically low (<10KB).";
        } else if (freeHeap < 30000) {
            s.level = HEALTH_WARNING;
            s.note = "Free heap is low (<30KB).";
        } else {
            s.level = HEALTH_NORMAL;
            s.note = "Free heap is healthy.";
        }
    }

    if (t.memory.heapFragmentationPct.status == MEAS_LIVE) {
        s.wasMeasured = true;
        float frag = t.memory.heapFragmentationPct.value;
        if (frag > 60.0f && s.level != HEALTH_CRITICAL) {
            s.level = HEALTH_WARNING;
            s.note = "Heap fragmentation is elevated.";
        }
    }

    return s;
}

SubsystemHealth Diagnostics::_evaluateWiFi(const ESP32Telemetry& t) const {
    SubsystemHealth s;
    s.name = "Wi-Fi";
    s.wasMeasured = false;
    s.level = HEALTH_UNKNOWN;
    s.note = "Wi-Fi not connected or not measured.";

    if (t.wifi.connected.status == MEAS_LIVE) {
        s.wasMeasured = true;
        if (!t.wifi.connected.value) {
            s.level = HEALTH_INFO;
            s.note = "Wi-Fi is not connected.";
            return s;
        }
        if (t.wifi.rssiDbm.status == MEAS_LIVE) {
            int8_t rssi = t.wifi.rssiDbm.value;
            if (rssi >= -60) {
                s.level = HEALTH_NORMAL;
                s.note = "Wi-Fi signal is good.";
            } else if (rssi >= -75) {
                s.level = HEALTH_WARNING;
                s.note = "Weak Wi-Fi signal.";
            } else {
                s.level = HEALTH_HIGH;
                s.note = "Very weak Wi-Fi signal.";
            }
        } else {
            s.level = HEALTH_NORMAL;
            s.note = "Wi-Fi connected; signal strength not measured.";
        }
    }

    return s;
}

SubsystemHealth Diagnostics::_evaluateCommunication(const ESP32Telemetry& t) const {
    SubsystemHealth s;
    s.name = "Communication";
    s.wasMeasured = true; // error counters are always initialized (even if zero) by Telemetry
    uint32_t total = t.errors.communicationErrors;
    if (total == 0) {
        s.level = HEALTH_NORMAL;
        s.note = "No communication errors recorded.";
    } else if (total < 5) {
        s.level = HEALTH_WARNING;
        s.note = String(total) + " communication error(s) recorded.";
    } else {
        s.level = HEALTH_HIGH;
        s.note = String(total) + " communication errors recorded.";
    }
    return s;
}

SubsystemHealth Diagnostics::_evaluateSystem(const ESP32Telemetry& t) const {
    SubsystemHealth s;
    s.name = "System";
    s.wasMeasured = false;
    s.level = HEALTH_UNKNOWN;
    s.note = "Uptime/reset reason not measured.";

    if (t.system.uptimeMs.status == MEAS_LIVE) {
        s.wasMeasured = true;
        s.level = HEALTH_NORMAL;
        s.note = "System uptime nominal.";
    }
    if (t.system.resetReason.status == MEAS_LIVE) {
        s.wasMeasured = true;
        String reason = t.system.resetReason.value;
        if (reason == "PANIC" || reason == "TASK_WATCHDOG" || reason == "INT_WATCHDOG" || reason == "WATCHDOG") {
            s.level = HEALTH_WARNING;
            s.note = "Last reset was caused by: " + reason;
        } else if (s.level != HEALTH_WARNING) {
            s.level = HEALTH_NORMAL;
        }
    }
    return s;
}

HealthReport Diagnostics::buildHealthReport(const ESP32Telemetry& t) const {
    HealthReport report;
    report.subsystemCount = 0;
    report.generatedAtMs = millis();
    report.mainIssue = "";

    SubsystemHealth results[5];
    results[0] = _evaluateCpu(t);
    results[1] = _evaluateMemory(t);
    results[2] = _evaluateWiFi(t);
    results[3] = _evaluateCommunication(t);
    results[4] = _evaluateSystem(t);

    int totalScore = 0;
    int measuredCount = 0;
    HealthLevel worstLevel = HEALTH_NORMAL;
    String worstNote = "";
    int worstRank = -1; // higher rank = worse

    for (uint8_t i = 0; i < 5 && report.subsystemCount < HealthReport::MAX_SUBSYSTEMS; i++) {
        report.subsystems[report.subsystemCount++] = results[i];

        if (results[i].wasMeasured) {
            int subScore;
            switch (results[i].level) {
                case HEALTH_NORMAL: subScore = 100; break;
                case HEALTH_INFO: subScore = 85; break;
                case HEALTH_WARNING: subScore = 60; break;
                case HEALTH_HIGH: subScore = 35; break;
                case HEALTH_CRITICAL: subScore = 10; break;
                default: subScore = -1; break;
            }
            if (subScore >= 0) {
                totalScore += subScore;
                measuredCount++;

                int rank;
                switch (results[i].level) {
                    case HEALTH_CRITICAL: rank = 4; break;
                    case HEALTH_HIGH: rank = 3; break;
                    case HEALTH_WARNING: rank = 2; break;
                    case HEALTH_INFO: rank = 1; break;
                    default: rank = 0; break;
                }
                if (rank > worstRank) {
                    worstRank = rank;
                    worstLevel = results[i].level;
                    worstNote = results[i].name + ": " + results[i].note;
                }
            }
        }
    }

    if (measuredCount > 0) {
        report.overallScore = (uint8_t)(totalScore / measuredCount);
        report.overallLevel = scoreToLevel(report.overallScore);
    } else {
        report.overallScore = 0;
        report.overallLevel = HEALTH_UNKNOWN;
    }

    if (worstRank >= 2) { // WARNING or worse
        report.mainIssue = worstNote;
    }

    return report;
}

DiagnosticsReport Diagnostics::buildReport(const ESP32Telemetry& telemetry, bool wasFullScan) const {
    DiagnosticsReport report;
    report.telemetry = telemetry;
    report.wasFullScan = wasFullScan;
    report.health = buildHealthReport(telemetry);

    String summary = "Health: " + String(report.health.overallScore) + "/100 (" +
                      healthLevelToString(report.health.overallLevel) + ")";
    if (report.health.mainIssue.length() > 0) {
        summary += " | Main issue: " + report.health.mainIssue;
    }
    report.summary = summary;
    return report;
}
