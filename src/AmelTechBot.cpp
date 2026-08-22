// =============================================================
// AmelTechBot.cpp
// =============================================================
#include "AmelTechBot.h"

const char* ameltechStatusToString(AmelTechStatus status) {
    switch (status) {
        case AMELTECH_OK: return "OK";
        case AMELTECH_INVALID_INPUT: return "INVALID_INPUT";
        case AMELTECH_NOT_FOUND: return "NOT_FOUND";
        case AMELTECH_LOW_CONFIDENCE: return "LOW_CONFIDENCE";
        case AMELTECH_UNSUPPORTED: return "UNSUPPORTED";
        case AMELTECH_UNAVAILABLE: return "UNAVAILABLE";
        case AMELTECH_MEASUREMENT_ERROR: return "MEASUREMENT_ERROR";
        case AMELTECH_MEMORY_ERROR: return "MEMORY_ERROR";
        case AMELTECH_STORAGE_ERROR: return "STORAGE_ERROR";
        case AMELTECH_TIMEOUT: return "TIMEOUT";
        case AMELTECH_INVALID_CONFIGURATION: return "INVALID_CONFIGURATION";
        case AMELTECH_DUPLICATE: return "DUPLICATE";
        case AMELTECH_CONTRADICTION: return "CONTRADICTION";
        default: return "UNKNOWN";
    }
}

AmelTechBot::AmelTechBot()
    : _began(false), _trollingEnabled(false),
      _lastError(AMELTECH_OK), _lastStatus(AMELTECH_OK), _lastConfidence(0.0f),
      _lastMeasurementStatus(MEAS_UNAVAILABLE),
      _contextSize(AMELTECH_DEFAULT_CONTEXT_SIZE), _contextCount(0), _contextHead(0) {}

AmelTechStatus AmelTechBot::begin() {
    _knowledge.begin();
    _telemetry.begin();
    _diagnostics.begin();

    // Attempt to load persisted user knowledge; UNSUPPORTED on non-ESP32
    // host builds is expected and not treated as a hard failure.
    AmelTechStatus loadStatus = _knowledge.loadFromNVS();
    _began = true;
    _lastStatus = AMELTECH_OK;
    _lastError = AMELTECH_OK;

    if (loadStatus == AMELTECH_STORAGE_ERROR) {
        _lastError = AMELTECH_STORAGE_ERROR;
        return AMELTECH_STORAGE_ERROR;
    }
    return AMELTECH_OK;
}

// ---------------------------------------------------------------
// Context memory (bounded ring buffer)
// ---------------------------------------------------------------
void AmelTechBot::_pushContext(const String& question, const String& answer, const String& category,
                                bool hasNumeric, float numericValue, const String& numericUnit) {
    if (_contextSize == 0) return;

    uint8_t idx = _contextHead;
    _context[idx].question = question;
    _context[idx].answer = answer;
    _context[idx].category = category;
    _context[idx].hasNumeric = hasNumeric;
    _context[idx].numericValue = numericValue;
    _context[idx].numericUnit = numericUnit;

    _contextHead = (uint8_t)((_contextHead + 1) % _contextSize);
    if (_contextCount < _contextSize) _contextCount++;
}

bool AmelTechBot::_resolveContextReference(const String& normalizedInput, String& outResolved) const {
    // Very small set of deictic reference cues; deterministic, no guessing
    // beyond substituting the most recent context's subject matter.
    bool referencesPrevious =
        normalizedInput.indexOf("that") >= 0 ||
        normalizedInput.indexOf("it") >= 0 ||
        normalizedInput.indexOf("this") >= 0;

    if (!referencesPrevious || _contextCount == 0) return false;

    // Most recent entry is at (_contextHead - 1 + _contextSize) % _contextSize
    uint8_t lastIdx = (uint8_t)((_contextHead + _contextSize - 1) % _contextSize);
    const ContextEntry& last = _context[lastIdx];

    // "is that good" / "is it good" -> qualitative judgment about last numeric value
    if (normalizedInput.indexOf("good") >= 0 && last.hasNumeric) {
        outResolved = "__CONTEXT_JUDGE_NUMERIC__";
        return true;
    }

    outResolved = last.question; // fall back to re-answering the prior question's topic
    return false; // not a special-cased resolution; let normal query proceed on fallback if needed
}

// ---------------------------------------------------------------
// Trolling mode: appends optional harmless commentary AFTER a real
// answer. Never replaces warnings/critical content (spec item 32).
// ---------------------------------------------------------------
String AmelTechBot::_applyTrolling(const String& baseAnswer, const String& category) const {
    if (!_trollingEnabled) return baseAnswer;

    // Keep humor generic, technically harmless, and clearly secondary.
    static const char* jokes[] = {
        " (Beep boop. Still faster than your Wi-Fi router's setup wizard.)",
        " (Delivered at the speed of a 240MHz dual-core... showing off a bit.)",
        " (Computed with zero cloud calls and maximum sass.)",
    };
    static uint8_t counter = 0;
    String joke = String(jokes[counter % 3]);
    counter++;
    return baseAnswer + joke;
}

// ---------------------------------------------------------------
// Hardware/telemetry question routing: lightweight intent detection
// for common hardware questions so they can be answered from real
// telemetry rather than the static knowledge base.
// ---------------------------------------------------------------
String AmelTechBot::_handleHardwareQuestion(const String& normalizedInput, bool& handled) {
    handled = false;

    bool asksRssi = normalizedInput.indexOf("rssi") >= 0 ||
                     (normalizedInput.indexOf("wifi") >= 0 && normalizedInput.indexOf("signal") >= 0);
    bool asksHeap = normalizedInput.indexOf("free heap") >= 0 || normalizedInput.indexOf("free memory") >= 0 ||
                     normalizedInput.indexOf("ram") >= 0;
    bool asksUptime = normalizedInput.indexOf("uptime") >= 0 || normalizedInput.indexOf("how long") >= 0;
    bool asksCpuFreq = normalizedInput.indexOf("cpu frequency") >= 0 || normalizedInput.indexOf("clock speed") >= 0;
    bool asksHealth = normalizedInput.indexOf("health") >= 0 && normalizedInput.indexOf("esp32") >= 0;
    bool asksTemp = (normalizedInput.indexOf("temperature") >= 0 || normalizedInput.indexOf("how hot") >= 0) &&
                     normalizedInput.indexOf("boiling") < 0 && normalizedInput.indexOf("freezing") < 0;

    if (!(asksRssi || asksHeap || asksUptime || asksCpuFreq || asksHealth || asksTemp)) {
        return "";
    }

    handled = true;
    ESP32Telemetry t = _telemetry.sampleFast();

    if (asksHealth) {
        HealthReport h = _diagnostics.buildHealthReport(t);
        _lastMeasurementStatus = MEAS_LIVE;
        String msg = "ESP32 Health: " + String(h.overallScore) + "/100 (" +
                     healthLevelToString(h.overallLevel) + ")";
        if (h.mainIssue.length() > 0) {
            msg += ". Main issue: " + h.mainIssue;
        }
        _pushContext(normalizedInput, msg, "esp32", false, 0, "");
        return msg;
    }

    if (asksRssi) {
        if (t.wifi.rssiDbm.status == MEAS_LIVE) {
            _lastMeasurementStatus = MEAS_LIVE;
            String msg = "Your Wi-Fi RSSI is " + String(t.wifi.rssiDbm.value) + " dBm (" +
                         t.wifi.signalQuality.value + ").";
            _pushContext(normalizedInput, msg, "esp32", true, (float)t.wifi.rssiDbm.value, "dBm");
            return _applyTrolling(msg, "esp32");
        } else {
            _lastMeasurementStatus = t.wifi.rssiDbm.status;
            String msg = "Wi-Fi RSSI is currently " + String(measurementStatusToString(t.wifi.rssiDbm.status)) +
                         " (device may not be connected to Wi-Fi).";
            _pushContext(normalizedInput, msg, "esp32", false, 0, "");
            return msg;
        }
    }

    if (asksHeap) {
        if (t.memory.freeHeapBytes.status == MEAS_LIVE) {
            _lastMeasurementStatus = MEAS_LIVE;
            String msg = "Free heap is " + String(t.memory.freeHeapBytes.value) + " bytes.";
            _pushContext(normalizedInput, msg, "esp32", true, (float)t.memory.freeHeapBytes.value, "bytes");
            return _applyTrolling(msg, "esp32");
        } else {
            _lastMeasurementStatus = t.memory.freeHeapBytes.status;
            return "Free heap is currently " + String(measurementStatusToString(t.memory.freeHeapBytes.status)) + ".";
        }
    }

    if (asksUptime) {
        if (t.system.uptimeMs.status == MEAS_LIVE) {
            _lastMeasurementStatus = MEAS_LIVE;
            unsigned long ms = t.system.uptimeMs.value;
            unsigned long seconds = ms / 1000;
            String msg = "System uptime is " + String(seconds) + " seconds.";
            _pushContext(normalizedInput, msg, "esp32", true, (float)seconds, "seconds");
            return _applyTrolling(msg, "esp32");
        } else {
            _lastMeasurementStatus = t.system.uptimeMs.status;
            return "Uptime is currently " + String(measurementStatusToString(t.system.uptimeMs.status)) + ".";
        }
    }

    if (asksCpuFreq) {
        if (t.cpu.frequencyMHz.status == MEAS_LIVE) {
            _lastMeasurementStatus = MEAS_LIVE;
            String msg = "CPU frequency is " + String(t.cpu.frequencyMHz.value) + " MHz.";
            _pushContext(normalizedInput, msg, "esp32", true, (float)t.cpu.frequencyMHz.value, "MHz");
            return _applyTrolling(msg, "esp32");
        } else {
            _lastMeasurementStatus = t.cpu.frequencyMHz.status;
            return "CPU frequency is currently " + String(measurementStatusToString(t.cpu.frequencyMHz.status)) + ".";
        }
    }

    if (asksTemp) {
        if (t.temperature.internalTempC.status == MEAS_LIVE) {
            _lastMeasurementStatus = MEAS_LIVE;
            String msg = "Internal chip temperature is " + String(t.temperature.internalTempC.value) + " C.";
            _pushContext(normalizedInput, msg, "esp32", true, t.temperature.internalTempC.value, "C");
            return _applyTrolling(msg, "esp32");
        } else {
            _lastMeasurementStatus = t.temperature.internalTempC.status;
            return "Internal temperature reading is " +
                   String(measurementStatusToString(t.temperature.internalTempC.status)) +
                   " on this chip/build.";
        }
    }

    handled = false;
    return "";
}

// ---------------------------------------------------------------
// ask()
// ---------------------------------------------------------------
String AmelTechBot::ask(const String& input) {
    if (!_began) {
        _lastError = AMELTECH_INVALID_CONFIGURATION;
        _lastStatus = AMELTECH_INVALID_CONFIGURATION;
        return "Bot not initialized. Call begin() in setup() first.";
    }

    String trimmed = input;
    trimmed.trim();
    if (trimmed.length() == 0) {
        _lastError = AMELTECH_INVALID_INPUT;
        _lastStatus = AMELTECH_INVALID_INPUT;
        _lastConfidence = 0.0f;
        return "Please provide a question.";
    }

    String normalizedInput = KnowledgeBase::normalize(trimmed);

    // 1) Contextual "is that good?" style follow-ups
    String contextResolved;
    bool wasJudgeNumeric = _resolveContextReference(normalizedInput, contextResolved);
    if (contextResolved == "__CONTEXT_JUDGE_NUMERIC__") {
        uint8_t lastIdx = (uint8_t)((_contextHead + _contextSize - 1) % _contextSize);
        const ContextEntry& last = _context[lastIdx];
        String judgement;
        if (last.numericUnit == "dBm") {
            judgement = (last.numericValue >= -60) ? "Yes, that's a good signal." :
                        (last.numericValue >= -75) ? "It's borderline — usable but not great." :
                        "No, that's a weak signal.";
        } else if (last.numericUnit == "bytes") {
            judgement = (last.numericValue >= 30000) ? "Yes, that's a healthy amount of free heap." :
                        "That's on the low side for free heap.";
        } else {
            judgement = "I can't judge that value without more context on what's 'good' for it.";
        }
        _lastStatus = AMELTECH_OK;
        _lastError = AMELTECH_OK;
        _lastConfidence = 0.8f;
        return judgement;
    }
    (void)wasJudgeNumeric;

    // 2) Calculator detection: if the input looks like a pure arithmetic
    //    expression (digits + operators only, ignoring spaces), route to
    //    the calculator instead of the knowledge engine.
    {
        String candidate = trimmed;
        candidate.replace(" ", "");
        bool looksArithmetic = candidate.length() > 0;
        bool hasDigit = false;
        for (size_t i = 0; i < candidate.length(); i++) {
            char c = candidate[i];
            if (isdigit((unsigned char)c)) hasDigit = true;
            if (!(isdigit((unsigned char)c) || c == '.' || c == '+' || c == '-' ||
                  c == '*' || c == '/' || c == '%' || c == '(' || c == ')')) {
                looksArithmetic = false;
                break;
            }
        }
        if (looksArithmetic && hasDigit) {
            CalcResult calcResult = _calculator.evaluate(trimmed);
            if (calcResult.valid) {
                _lastStatus = AMELTECH_OK;
                _lastError = AMELTECH_OK;
                _lastConfidence = 1.0f;
                String msg = String(calcResult.value);
                _pushContext(normalizedInput, msg, "math", true, (float)calcResult.value, "");
                return msg;
            } else {
                switch (calcResult.status) {
                    case CALC_ERROR_DIV_BY_ZERO: _lastError = AMELTECH_INVALID_INPUT; break;
                    default: _lastError = AMELTECH_INVALID_INPUT; break;
                }
                _lastStatus = AMELTECH_INVALID_INPUT;
                _lastConfidence = 0.0f;
                return "Calculation error: " + calcResult.message;
            }
        }
    }

    // 3) Hardware/telemetry-backed questions
    bool hardwareHandled = false;
    String hardwareAnswer = _handleHardwareQuestion(normalizedInput, hardwareHandled);
    if (hardwareHandled) {
        _lastStatus = AMELTECH_OK;
        _lastError = AMELTECH_OK;
        _lastConfidence = 0.95f;
        return hardwareAnswer;
    }

    // 4) Knowledge base lookup with confidence thresholds
    MatchResult match = _knowledge.query(trimmed);
    _lastConfidence = match.confidence;

    if (!match.found || match.confidence < 0.50f) {
        _lastStatus = AMELTECH_LOW_CONFIDENCE;
        _lastError = AMELTECH_OK;
        return "I don't have enough reliable information to answer that.";
    }

    if (match.confidence < 0.75f) {
        _lastStatus = AMELTECH_LOW_CONFIDENCE;
        _lastError = AMELTECH_OK;
        return "I'm not fully sure, but did you mean: \"" + match.matchedQuestion + "\"? " +
               "Try rephrasing for a more confident answer.";
    }

    _lastStatus = AMELTECH_OK;
    _lastError = AMELTECH_OK;
    _pushContext(normalizedInput, match.answer, match.category, false, 0, "");
    return _applyTrolling(match.answer, match.category);
}

// ---------------------------------------------------------------
// Training / knowledge management
// ---------------------------------------------------------------
AmelTechStatus AmelTechBot::train(const String& question, const String& answer, const String& category) {
    TrainResult result = _knowledge.train(question, answer, category);
    _lastStatus = result.status;
    _lastError = (result.status == AMELTECH_OK) ? AMELTECH_OK : result.status;
    return result.status;
}

AmelTechStatus AmelTechBot::addQA(const String& question, const String& answer, const String& category) {
    return train(question, answer, category);
}

AmelTechStatus AmelTechBot::removeQA(const String& question) {
    AmelTechStatus status = _knowledge.removeQA(question);
    _lastStatus = status;
    _lastError = status;
    return status;
}

AmelTechStatus AmelTechBot::clearKnowledge() {
    _knowledge.clearUserKnowledge();
    _lastStatus = AMELTECH_OK;
    _lastError = AMELTECH_OK;
    return AMELTECH_OK;
}

size_t AmelTechBot::getKnowledgeCount() const {
    return _knowledge.totalCount();
}

AmelTechStatus AmelTechBot::saveKnowledge() {
    AmelTechStatus status = _knowledge.saveToNVS();
    _lastStatus = status;
    _lastError = status;
    return status;
}

AmelTechStatus AmelTechBot::loadKnowledge() {
    AmelTechStatus status = _knowledge.loadFromNVS();
    _lastStatus = status;
    _lastError = status;
    return status;
}

// ---------------------------------------------------------------
// Trolling mode
// ---------------------------------------------------------------
void AmelTechBot::enableTrolling(bool enabled) {
    _trollingEnabled = enabled;
}

bool AmelTechBot::isTrollingEnabled() const {
    return _trollingEnabled;
}

// ---------------------------------------------------------------
// Status introspection
// ---------------------------------------------------------------
AmelTechStatus AmelTechBot::getLastError() const { return _lastError; }
AmelTechStatus AmelTechBot::getLastStatus() const { return _lastStatus; }
float AmelTechBot::getConfidence() const { return _lastConfidence; }

AmelTechConfidenceTier AmelTechBot::getConfidenceTier() const {
    if (_lastConfidence >= 0.90f) return AMELTECH_CONF_STRONG;
    if (_lastConfidence >= 0.75f) return AMELTECH_CONF_MODERATE;
    if (_lastConfidence >= 0.50f) return AMELTECH_CONF_CLARIFY;
    return AMELTECH_CONF_UNKNOWN;
}

AmelTechMeasurementStatus AmelTechBot::getMeasurementStatus() const {
    return _lastMeasurementStatus;
}

// ---------------------------------------------------------------
// Telemetry / diagnostics / health
// ---------------------------------------------------------------
ESP32Telemetry AmelTechBot::getTelemetry(bool fullScan) {
    return fullScan ? _telemetry.sampleFull() : _telemetry.sampleFast();
}

DiagnosticsReport AmelTechBot::runDiagnostics(bool fullScan) {
    ESP32Telemetry t = fullScan ? _telemetry.sampleFull() : _telemetry.sampleFast();
    return _diagnostics.buildReport(t, fullScan);
}

HealthReport AmelTechBot::getHealthReport() {
    ESP32Telemetry t = _telemetry.sampleFast();
    return _diagnostics.buildHealthReport(t);
}

// ---------------------------------------------------------------
// Calculator
// ---------------------------------------------------------------
CalcResult AmelTechBot::calculate(const String& expression) {
    CalcResult result = _calculator.evaluate(expression);
    _lastStatus = result.valid ? AMELTECH_OK : AMELTECH_INVALID_INPUT;
    _lastError = _lastStatus;
    return result;
}

// ---------------------------------------------------------------
// Context memory controls
// ---------------------------------------------------------------
void AmelTechBot::resetContext() {
    _contextCount = 0;
    _contextHead = 0;
    for (uint8_t i = 0; i < AMELTECH_MAX_CONTEXT_SIZE; i++) {
        _context[i].question = "";
        _context[i].answer = "";
        _context[i].category = "";
        _context[i].hasNumeric = false;
    }
}

AmelTechStatus AmelTechBot::setContextSize(uint8_t size) {
    if (size == 0) {
        _lastError = AMELTECH_INVALID_INPUT;
        _lastStatus = AMELTECH_INVALID_INPUT;
        return AMELTECH_INVALID_INPUT;
    }
    if (size > AMELTECH_MAX_CONTEXT_SIZE) {
        size = AMELTECH_MAX_CONTEXT_SIZE;
    }
    _contextSize = size;
    resetContext();
    _lastError = AMELTECH_OK;
    _lastStatus = AMELTECH_OK;
    return AMELTECH_OK;
}

uint8_t AmelTechBot::getContextSize() const {
    return _contextSize;
}
