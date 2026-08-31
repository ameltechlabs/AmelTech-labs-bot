#include "Diagnostics.h"
#include "AmelTechLog.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

Diagnostics::Diagnostics()
    : _telem(nullptr),
      _thermal(nullptr),
      _sensors(nullptr),
      _begun(false),
      _emaScore(0),
      _hasEma(false) {
    memset(&_report, 0, sizeof(_report));
    _report.overallLevel = HEALTH_UNKNOWN;
}

void Diagnostics::begin(Telemetry* telemetry, ThermalGuard* thermal, SensorHub* sensors) {
    _telem = telemetry;
    _thermal = thermal;
    _sensors = sensors;
    _begun = true;
}

const char* Diagnostics::levelToString(HealthLevel l) {
    switch (l) {
        case HEALTH_NORMAL:   return "NORMAL";
        case HEALTH_INFO:     return "INFO";
        case HEALTH_WARNING:  return "WARNING";
        case HEALTH_HIGH:     return "HIGH";
        case HEALTH_CRITICAL: return "CRITICAL";
        default:              return "UNKNOWN";
    }
}

void Diagnostics::initComponent(HealthComponent& c, const char* name, uint8_t weight) {
    c.name = name;
    c.score = 0;
    c.confidence = 0;
    c.weight = weight;
    c.level = HEALTH_UNKNOWN;
    c.detail[0] = '\0';
    c.action[0] = '\0';
}

HealthLevel Diagnostics::levelForScore(uint8_t score) {
    if (score >= 88) return HEALTH_NORMAL;
    if (score >= 75) return HEALTH_INFO;
    if (score >= 58) return HEALTH_WARNING;
    if (score >= 38) return HEALTH_HIGH;
    return HEALTH_CRITICAL;
}

// Maps a measurement onto 0-100 with a smooth curve instead of hard steps, so
// small changes produce small score changes.
uint8_t Diagnostics::curveScore(float value, float best, float worst) {
    if (best == worst) return 100;
    float t;
    if (best > worst) {
        // higher is better
        t = (value - worst) / (best - worst);
    } else {
        // lower is better
        t = (worst - value) / (worst - best);
    }
    if (t <= 0.0f) return 0;
    if (t >= 1.0f) return 100;
    // Slight ease-out: the top of the range is comfortable, the bottom is not.
    float eased = 1.0f - (1.0f - t) * (1.0f - t) * 0.6f - (1.0f - t) * 0.4f;
    if (eased < 0.0f) eased = 0.0f;
    if (eased > 1.0f) eased = 1.0f;
    return (uint8_t)(eased * 100.0f + 0.5f);
}

// ---------------------------------------------------------------------------
void Diagnostics::scoreCpu(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "CPU", 8);

    if (!Telemetry::statusIsUsable(t.cpuFreqMhz.status)) {
        snprintf(c.detail, sizeof(c.detail), "frequency %s",
                 Telemetry::statusToString(t.cpuFreqMhz.status));
        snprintf(c.action, sizeof(c.action), "No action: this build cannot read the clock.");
        return;
    }

    uint32_t mhz = t.cpuFreqMhz.value;
    c.confidence = 90;
    bool throttled = (_thermal && _thermal->isThrottling());

    if (throttled) {
        c.score = 68;
        snprintf(c.detail, sizeof(c.detail), "%u MHz (thermally throttled)", (unsigned)mhz);
        snprintf(c.action, sizeof(c.action),
                 "Improve airflow or reduce workload; full speed returns once it cools.");
    } else if (mhz >= 160) {
        c.score = 96;
        snprintf(c.detail, sizeof(c.detail), "%u MHz", (unsigned)mhz);
        snprintf(c.action, sizeof(c.action), "None needed.");
    } else if (mhz >= 80) {
        c.score = 88;
        snprintf(c.detail, sizeof(c.detail), "%u MHz (power saving)", (unsigned)mhz);
        snprintf(c.action, sizeof(c.action),
                 "Fine for chat; raise the clock if responses feel slow.");
    } else {
        c.score = 70;
        snprintf(c.detail, sizeof(c.detail), "%u MHz (very low)", (unsigned)mhz);
        snprintf(c.action, sizeof(c.action),
                 "Below 80 MHz Wi-Fi is unreliable. Raise the CPU frequency.");
    }

    if (_thermal && _thermal->dutyPercent() > 80) {
        if (c.score > 72) c.score = 72;
        snprintf(c.action, sizeof(c.action),
                 "Duty cycle above 80%%: add a delay in loop() so the core can idle.");
    }
    c.level = levelForScore(c.score);
}

void Diagnostics::scoreMemory(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "Memory", 26);

    if (!Telemetry::statusIsUsable(t.freeHeap.status)) {
        snprintf(c.detail, sizeof(c.detail), "heap %s",
                 Telemetry::statusToString(t.freeHeap.status));
        snprintf(c.action, sizeof(c.action), "No action: heap statistics are unavailable.");
        return;
    }

    uint32_t freeH = t.freeHeap.value;
    uint32_t total = Telemetry::statusIsUsable(t.heapSize.status) ? t.heapSize.value : 0;
    c.confidence = 95;

    // Primary signal: absolute free heap, on a curve from 8 KB to 160 KB.
    int score = (int)curveScore((float)freeH, 160000.0f, 8000.0f);

    // Secondary: proportion of the heap still free.
    if (total > 0) {
        float ratio = (float)freeH / (float)total;
        int ratioScore = (int)curveScore(ratio, 0.55f, 0.05f);
        score = (score * 2 + ratioScore) / 3;
    }

    // Fragmentation makes a nominally healthy heap unusable for big blocks.
    if (Telemetry::statusIsUsable(t.heapFragmentationPct.status)) {
        uint32_t frag = t.heapFragmentationPct.value;
        if (frag > 70) score -= 22;
        else if (frag > 50) score -= 12;
        else if (frag > 35) score -= 5;
    }

    // The low-water mark reveals pressure that a snapshot would miss.
    if (Telemetry::statusIsUsable(t.minFreeHeap.status)) {
        if (t.minFreeHeap.value < 8000) score -= 18;
        else if (t.minFreeHeap.value < 16000) score -= 8;
    }

    if (score < 0) score = 0;
    if (score > 100) score = 100;
    c.score = (uint8_t)score;
    c.level = levelForScore(c.score);

    if (total > 0) {
        snprintf(c.detail, sizeof(c.detail), "%u KB free of %u KB",
                 (unsigned)(freeH / 1024), (unsigned)(total / 1024));
    } else {
        snprintf(c.detail, sizeof(c.detail), "%u KB free", (unsigned)(freeH / 1024));
    }

    if (c.level >= HEALTH_HIGH) {
        snprintf(c.action, sizeof(c.action),
                 "Free heap critical. Cut String use, shrink buffers, clear history.");
    } else if (c.level == HEALTH_WARNING) {
        snprintf(c.action, sizeof(c.action),
                 "Heap is tight. Training is blocked below the reserved minimum.");
    } else if (Telemetry::statusIsUsable(t.heapFragmentationPct.status) &&
               t.heapFragmentationPct.value > 50) {
        snprintf(c.action, sizeof(c.action),
                 "Heap is %u%% fragmented; prefer fixed buffers to reallocation.",
                 (unsigned)(t.heapFragmentationPct.value & 0xFF));
    } else {
        snprintf(c.action, sizeof(c.action), "None needed.");
    }
}

void Diagnostics::scoreThermal(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "Thermal", 18);

    float temp = NAN;
    const char* src = "die sensor";

    if (Telemetry::statusIsUsable(t.temperatureC.status)) {
        temp = t.temperatureC.value;
        c.confidence = 90;
    } else if (Telemetry::statusIsUsable(t.ambientTemperatureC.status)) {
        temp = t.ambientTemperatureC.value + 15.0f;
        src = "ambient estimate";
        // An estimate is worth including but should not dominate the score.
        c.confidence = 55;
    } else {
        snprintf(c.detail, sizeof(c.detail), "no readable temperature source");
        snprintf(c.action, sizeof(c.action),
                 "Attach a DHT sensor for ambient context; this chip has no usable die sensor.");
        return;
    }

    if (temp >= AMELTECH_THERMAL_CRITICAL_C)      c.score = 18;
    else if (temp >= AMELTECH_THERMAL_HIGH_C)     c.score = 42;
    else if (temp >= AMELTECH_THERMAL_WARN_C)     c.score = 66;
    else if (temp >= 55.0f)                       c.score = 86;
    else                                          c.score = 97;

    c.level = levelForScore(c.score);
    snprintf(c.detail, sizeof(c.detail), "%.1f C (%s)", (double)temp, src);

    if (c.level >= HEALTH_HIGH) {
        snprintf(c.action, sizeof(c.action),
                 "Too hot. Improve ventilation, avoid direct sun, and reduce continuous load.");
    } else if (c.level == HEALTH_WARNING) {
        snprintf(c.action, sizeof(c.action),
                 "Running warm. Automatic throttling engages at %d C.",
                 (int)AMELTECH_THERMAL_HIGH_C);
    } else {
        snprintf(c.action, sizeof(c.action), "None needed.");
    }
}

void Diagnostics::scoreWifi(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "Wi-Fi", 12);

    if (t.wifiConnected.status == MEAS_UNSUPPORTED) {
        snprintf(c.detail, sizeof(c.detail), "not supported in this build");
        snprintf(c.action, sizeof(c.action), "None: the bot runs fully offline.");
        return;
    }
    if (!Telemetry::statusIsUsable(t.wifiConnected.status)) {
        snprintf(c.detail, sizeof(c.detail), "status %s",
                 Telemetry::statusToString(t.wifiConnected.status));
        snprintf(c.action, sizeof(c.action), "None: Wi-Fi state could not be read.");
        return;
    }

    if (!t.wifiConnected.value) {
        // Being offline is normal for this library, not a fault.
        c.confidence = 40;
        c.score = 80;
        c.level = HEALTH_INFO;
        snprintf(c.detail, sizeof(c.detail), "not connected");
        snprintf(c.action, sizeof(c.action),
                 "None: all core features work without a network.");
        return;
    }

    if (!Telemetry::statusIsUsable(t.wifiRssi.status)) {
        c.confidence = 50;
        c.score = 85;
        c.level = HEALTH_INFO;
        snprintf(c.detail, sizeof(c.detail), "connected, RSSI unavailable");
        snprintf(c.action, sizeof(c.action), "None needed.");
        return;
    }

    c.confidence = 90;
    int rssi = t.wifiRssi.value;
    // -50 dBm is excellent, -85 dBm is unusable.
    c.score = curveScore((float)rssi, -50.0f, -88.0f);
    if (Telemetry::statusIsUsable(t.wifiDisconnectCount.status) &&
        t.wifiDisconnectCount.value >= 3) {
        int s = (int)c.score - 15;
        c.score = (uint8_t)(s < 0 ? 0 : s);
    }
    c.level = levelForScore(c.score);
    snprintf(c.detail, sizeof(c.detail), "%d dBm on %s", rssi,
             (t.wifiSsidStatus == MEAS_LIVE && t.wifiSsid[0]) ? t.wifiSsid : "unknown SSID");

    if (c.level >= HEALTH_HIGH) {
        snprintf(c.action, sizeof(c.action),
                 "Weak link. Move closer to the access point or add an external antenna.");
    } else if (c.level == HEALTH_WARNING) {
        snprintf(c.action, sizeof(c.action),
                 "Marginal signal; expect occasional retries. Check for 2.4 GHz interference.");
    } else {
        snprintf(c.action, sizeof(c.action), "None needed.");
    }
}

void Diagnostics::scoreStorage(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "Storage", 8);

    if (!Telemetry::statusIsUsable(t.freeSketchSpace.status)) {
        snprintf(c.detail, sizeof(c.detail), "flash usage %s",
                 Telemetry::statusToString(t.freeSketchSpace.status));
        snprintf(c.action, sizeof(c.action), "No action: flash statistics unavailable.");
        return;
    }

    c.confidence = 85;
    uint32_t freeSpace = t.freeSketchSpace.value;
    uint32_t sketch = Telemetry::statusIsUsable(t.sketchSize.status) ? t.sketchSize.value : 0;

    if (freeSpace >= sketch && sketch > 0) {
        c.score = 96;   // room for an OTA update
        snprintf(c.action, sizeof(c.action), "None needed; OTA has room.");
    } else if (freeSpace > 65536) {
        c.score = 80;
        snprintf(c.action, sizeof(c.action),
                 "Not enough spare flash for an OTA image of this sketch.");
    } else {
        c.score = 55;
        snprintf(c.action, sizeof(c.action),
                 "Very little spare flash. Choose a partition scheme with a larger app area.");
    }
    c.level = levelForScore(c.score);
    snprintf(c.detail, sizeof(c.detail), "%u KB sketch, %u KB free",
             (unsigned)(sketch / 1024), (unsigned)(freeSpace / 1024));
}

void Diagnostics::scoreSensors(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "Sensors", 8);
    (void)t;

    if (!_sensors || !_sensors->isConfigured()) {
        snprintf(c.detail, sizeof(c.detail), "no DHT sensor configured");
        snprintf(c.action, sizeof(c.action),
                 "Optional: call beginDHT(pin, type) to add ambient sensing.");
        return;
    }

    const DhtReading& r = _sensors->reading();
    c.confidence = 90;

    uint32_t attempts = (uint32_t)r.successCount + (uint32_t)r.failureCount;
    if (attempts == 0) {
        c.confidence = 30;
        c.score = 70;
        c.level = HEALTH_INFO;
        snprintf(c.detail, sizeof(c.detail), "%s configured, not read yet", _sensors->typeName());
        snprintf(c.action, sizeof(c.action), "Call readSensors() from loop().");
        return;
    }

    uint32_t failPct = (uint32_t)r.failureCount * 100u / attempts;

    if (r.consecutiveFailures >= 3) {
        c.score = 22;
        snprintf(c.action, sizeof(c.action),
                 "Check the data pin, the 3V3 supply and the pull-up resistor on GPIO%u.",
                 (unsigned)_sensors->pin());
    } else if (failPct > 40) {
        c.score = 45;
        snprintf(c.action, sizeof(c.action),
                 "High error rate. Shorten the cable or add a 100nF cap across the sensor supply.");
    } else if (failPct > 15) {
        c.score = 70;
        snprintf(c.action, sizeof(c.action),
                 "Occasional read errors are normal for DHT parts; retries are already enabled.");
    } else if (!_sensors->isFresh(60000)) {
        c.score = 72;
        snprintf(c.action, sizeof(c.action), "Reading is stale; call readSensors() more often.");
    } else {
        c.score = 96;
        snprintf(c.action, sizeof(c.action), "None needed.");
    }

    c.level = levelForScore(c.score);
    snprintf(c.detail, sizeof(c.detail), "%s, %u ok / %u failed (%u%%)",
             _sensors->typeName(), (unsigned)r.successCount,
             (unsigned)r.failureCount, (unsigned)failPct);
}

void Diagnostics::scoreStability(HealthComponent& c, const ESP32Telemetry& t) {
    initComponent(c, "Stability", 20);

    bool haveReset = Telemetry::statusIsUsable(t.resetReason.status);
    bool haveErrors = Telemetry::statusIsUsable(t.errorCount.status);
    if (!haveReset && !haveErrors) {
        snprintf(c.detail, sizeof(c.detail), "no stability inputs available");
        snprintf(c.action, sizeof(c.action), "No action.");
        return;
    }

    c.confidence = haveReset ? 92 : 60;
    int score = 96;
    const char* resetName = "unknown";

    if (haveReset) {
        uint32_t r = t.resetReason.value;
        resetName = Telemetry::resetReasonToString(r);
#if defined(ESP32)
        if (r == ESP_RST_BROWNOUT) {
            score = 34;
            snprintf(c.action, sizeof(c.action),
                     "Brownout reset: the 3V3 rail sagged. Use a better USB cable or supply.");
        } else if (r == ESP_RST_PANIC) {
            score = 38;
            snprintf(c.action, sizeof(c.action),
                     "Last boot followed a crash. Check the serial backtrace from that reset.");
        } else if (r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT) {
            score = 42;
            snprintf(c.action, sizeof(c.action),
                     "Watchdog reset: something blocked the loop. Avoid long delays in callbacks.");
        }
#endif
    }

    if (haveErrors) {
        uint32_t errs = t.errorCount.value;
        if (errs > 50) score -= 30;
        else if (errs > 20) score -= 18;
        else if (errs > 5) score -= 8;
    }

    if (Telemetry::statusIsUsable(t.loopLatencyMs.status) && t.loopLatencyMs.value > 500) {
        score -= 12;
        snprintf(c.action, sizeof(c.action),
                 "Loop latency averages %u ms; long blocking calls risk a watchdog reset.",
                 (unsigned)t.loopLatencyMs.value);
    }

    if (Telemetry::statusIsUsable(t.taskStackHighWater.status) &&
        t.taskStackHighWater.value < 512) {
        score -= 20;
        snprintf(c.action, sizeof(c.action),
                 "Only %u bytes of task stack remain. Increase the loop task stack size.",
                 (unsigned)t.taskStackHighWater.value);
    }

    if (score < 0) score = 0;
    if (score > 100) score = 100;
    c.score = (uint8_t)score;
    c.level = levelForScore(c.score);

    uint32_t upSec = Telemetry::statusIsUsable(t.uptimeMs.status) ? (t.uptimeMs.value / 1000) : 0;
    snprintf(c.detail, sizeof(c.detail), "reset %s, up %us, %u errors",
             resetName, (unsigned)upSec,
             haveErrors ? (unsigned)t.errorCount.value : 0u);

    if (c.action[0] == '\0') {
        snprintf(c.action, sizeof(c.action), "None needed.");
    }
}

// ---------------------------------------------------------------------------
const HealthReport& Diagnostics::evaluateHealth() {
    memset(&_report, 0, sizeof(_report));
    _report.overallLevel = HEALTH_UNKNOWN;
    _report.componentCount = 0;

    if (!_telem) {
        snprintf(_report.summary, sizeof(_report.summary),
                 "Health unknown: telemetry is not attached.");
        snprintf(_report.mainIssue, sizeof(_report.mainIssue), "Diagnostics not initialised");
        return _report;
    }

    const ESP32Telemetry& t = _telem->data();

    scoreMemory(_report.components[0], t);
    scoreStability(_report.components[1], t);
    scoreThermal(_report.components[2], t);
    scoreWifi(_report.components[3], t);
    scoreCpu(_report.components[4], t);
    scoreStorage(_report.components[5], t);
    scoreSensors(_report.components[6], t);
    _report.componentCount = AMELTECH_HEALTH_COMPONENTS;

    // Confidence-weighted mean over measured components only.
    uint32_t weightedScore = 0;
    uint32_t effectiveWeight = 0;
    uint32_t totalWeight = 0;

    int worst = -1;
    for (uint8_t i = 0; i < _report.componentCount; ++i) {
        const HealthComponent& c = _report.components[i];
        totalWeight += c.weight;
        if (c.confidence == 0) continue;
        uint32_t w = (uint32_t)c.weight * c.confidence;
        weightedScore += (uint32_t)c.score * w;
        effectiveWeight += w;

        if (worst < 0) worst = i;
        else {
            const HealthComponent& b = _report.components[worst];
            if (c.level > b.level || (c.level == b.level && c.score < b.score)) worst = i;
        }
    }

    if (effectiveWeight == 0) {
        _report.overallScore = 0;
        _report.overallConfidence = 0;
        _report.overallLevel = HEALTH_UNKNOWN;
        snprintf(_report.mainIssue, sizeof(_report.mainIssue),
                 "Nothing measurable on this build");
        snprintf(_report.summary, sizeof(_report.summary),
                 "ESP32 health: UNKNOWN. No component could be measured.");
        return _report;
    }

    _report.overallScore = (int)(weightedScore / effectiveWeight);

    // Overall confidence is the share of total weight that was actually
    // measured. Each component's confidence is already a percentage, so
    // sum(weight * confidence) / sum(weight) lands back on 0-100.
    uint32_t confWeight = 0;
    for (uint8_t i = 0; i < _report.componentCount; ++i) {
        confWeight += (uint32_t)_report.components[i].weight * _report.components[i].confidence;
    }
    _report.overallConfidence = totalWeight ? (uint8_t)(confWeight / totalWeight) : 0;

    _report.overallLevel = levelForScore((uint8_t)_report.overallScore);

    // A single critical component must not be hidden by a good average.
    if (worst >= 0) {
        const HealthComponent& w = _report.components[worst];
        if (w.level > _report.overallLevel && w.confidence >= 50) {
            _report.overallLevel = (w.level == HEALTH_CRITICAL) ? HEALTH_HIGH : w.level;
        }
        if (w.level >= HEALTH_WARNING) {
            snprintf(_report.mainIssue, sizeof(_report.mainIssue), "%s - %s. %s",
                     w.name, w.detail, w.action);
        } else {
            snprintf(_report.mainIssue, sizeof(_report.mainIssue),
                     "No significant issues detected.");
        }
    }

    if (_hasEma) {
        int delta = _report.overallScore - _emaScore;
        if (delta > 127) delta = 127;
        if (delta < -127) delta = -127;
        _report.trendDelta = (int8_t)delta;
        _emaScore = (_emaScore * 3 + _report.overallScore) / 4;
    } else {
        _emaScore = _report.overallScore;
        _hasEma = true;
        _report.trendDelta = 0;
    }

    snprintf(_report.summary, sizeof(_report.summary),
             "ESP32 health %d/100 [%s], confidence %u%%. %s",
             _report.overallScore, levelToString(_report.overallLevel),
             (unsigned)_report.overallConfidence, _report.mainIssue);

    return _report;
}

int Diagnostics::healthScore() {
    return evaluateHealth().overallScore;
}

String Diagnostics::healthReportString() {
    const HealthReport& hr = evaluateHealth();
    String out;
    out.reserve(640);

    out += F("ESP32 Health: ");
    out += hr.overallScore;
    out += F("/100 [");
    out += levelToString(hr.overallLevel);
    out += F("]  confidence ");
    out += (int)hr.overallConfidence;
    out += F("%\n");

    if (hr.trendDelta > 2) out += F("Trend: improving\n");
    else if (hr.trendDelta < -2) out += F("Trend: worsening\n");

    for (uint8_t i = 0; i < hr.componentCount; ++i) {
        const HealthComponent& c = hr.components[i];
        out += c.name;
        out += F(": ");
        if (c.confidence == 0) {
            out += F("not measured - ");
            out += c.detail;
            out += '\n';
            continue;
        }
        out += levelToString(c.level);
        out += F(" (");
        out += (int)c.score;
        out += F(") - ");
        out += c.detail;
        out += '\n';
    }

    out += F("Main issue: ");
    out += hr.mainIssue;
    return out;
}

String Diagnostics::run(bool full) {
    if (!_telem) return String(F("Diagnostics unavailable: telemetry not attached."));

    if (full) _telem->updateFull(true);
    else _telem->updateFast(true);
    if (_thermal) _thermal->update();

    const ESP32Telemetry& t = _telem->data();
    String out;
    out.reserve(900);

    out += F("=== AmelTech Diagnostics v");
    out += F(AMELTECH_VERSION_STRING);
    out += F(" ===\n");

    out += F("Chip: ");
    out += (t.chipModelStatus == MEAS_LIVE) ? t.chipModel
                                            : Telemetry::statusToString(t.chipModelStatus);
    if (t.chipCoresStatus == MEAS_LIVE) {
        out += F(" x");
        out += (int)t.chipCores;
    }
    if (t.chipRevisionStatus == MEAS_LIVE) {
        out += F(" rev");
        out += (int)t.chipRevision;
    }

    out += F("\nCPU: ");
    if (Telemetry::statusIsUsable(t.cpuFreqMhz.status)) {
        out += (int)t.cpuFreqMhz.value;
        out += F(" MHz");
    } else out += Telemetry::statusToString(t.cpuFreqMhz.status);

    out += F("\nHeap: ");
    if (Telemetry::statusIsUsable(t.freeHeap.status)) {
        out += (int)(t.freeHeap.value / 1024);
        out += F(" KB free");
        if (Telemetry::statusIsUsable(t.heapSize.status)) {
            out += F(" of ");
            out += (int)(t.heapSize.value / 1024);
            out += F(" KB");
        }
        if (Telemetry::statusIsUsable(t.heapFragmentationPct.status)) {
            out += F(", ");
            out += (int)t.heapFragmentationPct.value;
            out += F("% fragmented");
        }
    } else out += Telemetry::statusToString(t.freeHeap.status);

    out += F("\nUptime: ");
    if (Telemetry::statusIsUsable(t.uptimeMs.status)) {
        out += (unsigned long)(t.uptimeMs.value / 1000);
        out += F(" s");
    } else out += Telemetry::statusToString(t.uptimeMs.status);

    out += F("\nWi-Fi: ");
    if (Telemetry::statusIsUsable(t.wifiConnected.status)) {
        out += t.wifiConnected.value ? F("connected") : F("disconnected");
        if (Telemetry::statusIsUsable(t.wifiRssi.status)) {
            out += F(" RSSI ");
            out += (int)t.wifiRssi.value;
            out += F(" dBm");
        }
    } else out += Telemetry::statusToString(t.wifiConnected.status);

    out += F("\nDie temperature: ");
    if (Telemetry::statusIsUsable(t.temperatureC.status)) {
        out += String(t.temperatureC.value, 1);
        out += F(" C");
    } else out += Telemetry::statusToString(t.temperatureC.status);

    out += F("\nAmbient: ");
    if (Telemetry::statusIsUsable(t.ambientTemperatureC.status)) {
        out += String(t.ambientTemperatureC.value, 1);
        out += F(" C / ");
        out += String(t.ambientHumidity.value, 0);
        out += F("% RH");
    } else out += Telemetry::statusToString(t.ambientTemperatureC.status);

    out += F("\nReset reason: ");
    if (Telemetry::statusIsUsable(t.resetReason.status)) {
        out += Telemetry::resetReasonToString(t.resetReason.value);
    } else out += Telemetry::statusToString(t.resetReason.status);

    if (full && _thermal) {
        out += '\n';
        out += _thermal->report();
    }

    out += '\n';
    out += healthReportString();

    if (full) {
        out += F("\nRecommended actions:\n");
        const HealthReport& hr = lastReport();
        uint8_t listed = 0;
        for (uint8_t i = 0; i < hr.componentCount; ++i) {
            const HealthComponent& c = hr.components[i];
            if (c.confidence == 0) continue;
            if (c.level < HEALTH_WARNING) continue;
            out += F("- ");
            out += c.name;
            out += F(": ");
            out += c.action;
            out += '\n';
            ++listed;
        }
        if (listed == 0) out += F("- None. Everything measurable is within normal limits.\n");
    }

    return out;
}
