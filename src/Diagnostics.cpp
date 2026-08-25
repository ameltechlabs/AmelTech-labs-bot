#include "Diagnostics.h"
#include <stdio.h>
#include <string.h>

Diagnostics::Diagnostics() : _telem(nullptr), _begun(false) {}

void Diagnostics::begin(Telemetry* telem) {
    _telem = telem;
    _begun = true;
}

const char* Diagnostics::levelToString(HealthLevel l) {
    switch (l) {
        case HEALTH_NORMAL: return "NORMAL";
        case HEALTH_INFO: return "INFO";
        case HEALTH_WARNING: return "WARNING";
        case HEALTH_HIGH: return "HIGH";
        case HEALTH_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void Diagnostics::scoreCpu(HealthComponent& c, const ESP32Telemetry& t) {
    c.name = "CPU";
    c.detail[0] = '\0';
    if (t.cpuFreqMhz.status != MEAS_LIVE && t.cpuFreqMhz.status != MEAS_CACHED) {
        c.score = 50;
        c.level = HEALTH_UNKNOWN;
        snprintf(c.detail, sizeof(c.detail), "CPU frequency %s", Telemetry::statusToString(t.cpuFreqMhz.status));
        return;
    }
    c.score = 95;
    c.level = HEALTH_NORMAL;
    snprintf(c.detail, sizeof(c.detail), "%u MHz", (unsigned)t.cpuFreqMhz.value);
}

void Diagnostics::scoreMemory(HealthComponent& c, const ESP32Telemetry& t) {
    c.name = "Memory";
    c.detail[0] = '\0';
    if (t.freeHeap.status != MEAS_LIVE && t.freeHeap.status != MEAS_CACHED) {
        c.score = 50;
        c.level = HEALTH_UNKNOWN;
        snprintf(c.detail, sizeof(c.detail), "Heap %s", Telemetry::statusToString(t.freeHeap.status));
        return;
    }
    uint32_t freeH = t.freeHeap.value;
    uint32_t total = (t.heapSize.status == MEAS_LIVE) ? t.heapSize.value : 0;
    if (freeH < 8192) {
        c.score = 20;
        c.level = HEALTH_CRITICAL;
        snprintf(c.detail, sizeof(c.detail), "Very low free heap: %u B", (unsigned)freeH);
    } else if (freeH < 16384) {
        c.score = 45;
        c.level = HEALTH_HIGH;
        snprintf(c.detail, sizeof(c.detail), "Low free heap: %u B", (unsigned)freeH);
    } else if (freeH < 32768) {
        c.score = 70;
        c.level = HEALTH_WARNING;
        snprintf(c.detail, sizeof(c.detail), "Moderate free heap: %u B", (unsigned)freeH);
    } else {
        c.score = 95;
        c.level = HEALTH_NORMAL;
        if (total > 0) {
            snprintf(c.detail, sizeof(c.detail), "Free heap %u / %u B", (unsigned)freeH, (unsigned)total);
        } else {
            snprintf(c.detail, sizeof(c.detail), "Free heap %u B", (unsigned)freeH);
        }
    }
}

void Diagnostics::scoreWifi(HealthComponent& c, const ESP32Telemetry& t) {
    c.name = "Wi-Fi";
    c.detail[0] = '\0';
    if (t.wifiConnected.status == MEAS_UNSUPPORTED) {
        c.score = 80;
        c.level = HEALTH_INFO;
        snprintf(c.detail, sizeof(c.detail), "Wi-Fi status unsupported on this build");
        return;
    }
    if (t.wifiConnected.status != MEAS_LIVE && t.wifiConnected.status != MEAS_CACHED) {
        c.score = 60;
        c.level = HEALTH_UNKNOWN;
        snprintf(c.detail, sizeof(c.detail), "Wi-Fi %s", Telemetry::statusToString(t.wifiConnected.status));
        return;
    }
    if (!t.wifiConnected.value) {
        c.score = 75;
        c.level = HEALTH_INFO;
        snprintf(c.detail, sizeof(c.detail), "Not connected");
        return;
    }
    if (t.wifiRssi.status != MEAS_LIVE && t.wifiRssi.status != MEAS_CACHED) {
        c.score = 80;
        c.level = HEALTH_INFO;
        snprintf(c.detail, sizeof(c.detail), "Connected (RSSI unavailable)");
        return;
    }
    int rssi = t.wifiRssi.value;
    if (rssi >= -55) {
        c.score = 95;
        c.level = HEALTH_NORMAL;
        snprintf(c.detail, sizeof(c.detail), "Strong signal %d dBm", rssi);
    } else if (rssi >= -65) {
        c.score = 85;
        c.level = HEALTH_NORMAL;
        snprintf(c.detail, sizeof(c.detail), "Good signal %d dBm", rssi);
    } else if (rssi >= -72) {
        c.score = 70;
        c.level = HEALTH_WARNING;
        snprintf(c.detail, sizeof(c.detail), "Fair signal %d dBm", rssi);
    } else if (rssi >= -80) {
        c.score = 50;
        c.level = HEALTH_HIGH;
        snprintf(c.detail, sizeof(c.detail), "Weak signal %d dBm", rssi);
    } else {
        c.score = 30;
        c.level = HEALTH_CRITICAL;
        snprintf(c.detail, sizeof(c.detail), "Very weak signal %d dBm", rssi);
    }
}

void Diagnostics::scoreCommunication(HealthComponent& c, const ESP32Telemetry& t) {
    c.name = "Communication";
    c.score = 90;
    c.level = HEALTH_NORMAL;
    if (t.errorCount.status == MEAS_LIVE && t.errorCount.value > 0) {
        if (t.errorCount.value > 50) {
            c.score = 40;
            c.level = HEALTH_HIGH;
            snprintf(c.detail, sizeof(c.detail), "Elevated error count: %u", (unsigned)t.errorCount.value);
        } else {
            c.score = 70;
            c.level = HEALTH_WARNING;
            snprintf(c.detail, sizeof(c.detail), "Some errors: %u", (unsigned)t.errorCount.value);
        }
    } else {
        snprintf(c.detail, sizeof(c.detail), "No elevated communication errors reported");
    }
}

void Diagnostics::scoreSystem(HealthComponent& c, const ESP32Telemetry& t) {
    c.name = "System";
    c.score = 90;
    c.level = HEALTH_NORMAL;
    if (t.resetReason.status == MEAS_LIVE) {
        uint32_t r = t.resetReason.value;
#if defined(ESP32)
        if (r == ESP_RST_BROWNOUT) {
            c.score = 40;
            c.level = HEALTH_HIGH;
            snprintf(c.detail, sizeof(c.detail), "Last reset: brownout");
            return;
        }
        if (r == ESP_RST_PANIC || r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT) {
            c.score = 45;
            c.level = HEALTH_HIGH;
            snprintf(c.detail, sizeof(c.detail), "Last reset: %s", Telemetry::resetReasonToString(r));
            return;
        }
#endif
        snprintf(c.detail, sizeof(c.detail), "Last reset: %s", Telemetry::resetReasonToString(t.resetReason.value));
    } else {
        snprintf(c.detail, sizeof(c.detail), "Reset reason %s", Telemetry::statusToString(t.resetReason.status));
        c.level = HEALTH_INFO;
        c.score = 80;
    }
}

HealthReport Diagnostics::evaluateHealth() {
    HealthReport hr;
    memset(&hr, 0, sizeof(hr));
    hr.overallScore = 0;
    hr.overallLevel = HEALTH_UNKNOWN;
    hr.componentCount = 0;
    hr.mainIssue[0] = '\0';
    hr.summary[0] = '\0';

    if (!_telem) {
        snprintf(hr.summary, sizeof(hr.summary), "Telemetry not available");
        return hr;
    }

    const ESP32Telemetry& t = _telem->data();
    HealthComponent comps[5];
    scoreCpu(comps[0], t);
    scoreMemory(comps[1], t);
    scoreWifi(comps[2], t);
    scoreCommunication(comps[3], t);
    scoreSystem(comps[4], t);

    int sum = 0;
    int worst = 0;
    HealthLevel worstLevel = HEALTH_NORMAL;
    for (int i = 0; i < 5; ++i) {
        hr.components[i] = comps[i];
        sum += comps[i].score;
        if (comps[i].level > worstLevel) {
            worstLevel = comps[i].level;
            worst = i;
        }
    }
    hr.componentCount = 5;
    hr.overallScore = sum / 5;
    hr.overallLevel = worstLevel;

    if (worstLevel >= HEALTH_WARNING) {
        snprintf(hr.mainIssue, sizeof(hr.mainIssue), "%s: %s", comps[worst].name, comps[worst].detail);
    } else {
        snprintf(hr.mainIssue, sizeof(hr.mainIssue), "No significant issues detected");
    }

    snprintf(hr.summary, sizeof(hr.summary),
             "ESP32 Health: %d/100 [%s]. %s",
             hr.overallScore, levelToString(hr.overallLevel), hr.mainIssue);
    return hr;
}

String Diagnostics::healthReportString() {
    HealthReport hr = evaluateHealth();
    String out;
    out.reserve(320);
    out += "ESP32 Health: ";
    out += hr.overallScore;
    out += "/100\n";
    for (int i = 0; i < hr.componentCount; ++i) {
        out += hr.components[i].name;
        out += ": ";
        out += levelToString(hr.components[i].level);
        out += " (";
        out += hr.components[i].score;
        out += ") - ";
        out += hr.components[i].detail;
        out += "\n";
    }
    out += "Main issue:\n";
    out += hr.mainIssue;
    return out;
}

String Diagnostics::run(bool full) {
    if (!_telem) return String("Diagnostics unavailable");
    if (full) {
        _telem->updateFull();
    } else {
        _telem->updateFast();
    }
    const ESP32Telemetry& t = _telem->data();
    String out;
    out.reserve(512);
    out += "=== AmelTech Diagnostics ===\n";
    out += "Chip: ";
    out += (t.chipModelStatus == MEAS_LIVE) ? t.chipModel : Telemetry::statusToString(t.chipModelStatus);
    out += "\nCores: ";
    if (t.chipCoresStatus == MEAS_LIVE) out += String(t.chipCores);
    else out += Telemetry::statusToString(t.chipCoresStatus);
    out += "\nCPU: ";
    if (t.cpuFreqMhz.status == MEAS_LIVE) {
        out += String(t.cpuFreqMhz.value);
        out += " MHz";
    } else out += Telemetry::statusToString(t.cpuFreqMhz.status);
    out += "\nFree heap: ";
    if (t.freeHeap.status == MEAS_LIVE) {
        out += String(t.freeHeap.value);
        out += " B";
    } else out += Telemetry::statusToString(t.freeHeap.status);
    out += "\nUptime: ";
    if (t.uptimeMs.status == MEAS_LIVE) {
        out += String(t.uptimeMs.value / 1000);
        out += " s";
    } else out += Telemetry::statusToString(t.uptimeMs.status);
    out += "\nWi-Fi: ";
    if (t.wifiConnected.status == MEAS_LIVE) {
        out += t.wifiConnected.value ? "connected" : "disconnected";
        if (t.wifiRssi.status == MEAS_LIVE) {
            out += " RSSI=";
            out += String(t.wifiRssi.value);
            out += " dBm";
        }
    } else out += Telemetry::statusToString(t.wifiConnected.status);
    out += "\nTemperature: ";
    if (t.temperatureC.status == MEAS_LIVE) {
        out += String(t.temperatureC.value, 1);
        out += " (limited accuracy)";
    } else out += Telemetry::statusToString(t.temperatureC.status);
    out += "\nReset: ";
    if (t.resetReason.status == MEAS_LIVE) {
        out += Telemetry::resetReasonToString(t.resetReason.value);
    } else out += Telemetry::statusToString(t.resetReason.status);
    out += "\n";
    out += healthReportString();
    return out;
}
