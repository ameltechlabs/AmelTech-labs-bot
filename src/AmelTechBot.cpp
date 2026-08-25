#include "AmelTechBot.h"
#include <string.h>
#include <ctype.h>

AmelTechBot::AmelTechBot()
    : _ready(false),
      _trolling(false),
      _lastError(AMELTECH_OK),
      _lastConfidence(0.0f),
      _lastMeasStatus(MEAS_UNAVAILABLE),
      _contextSize(2),
      _contextHead(0) {
    _lastStatusBuf[0] = '\0';
    memset(_context, 0, sizeof(_context));
}

AmelTechBot::~AmelTechBot() {
    end();
}

bool AmelTechBot::begin() {
    _kb.begin();
    _telem.begin();
    _diag.begin(&_telem);
    _ready = true;
    setError(AMELTECH_OK, "Ready");
    return true;
}

void AmelTechBot::end() {
    _ready = false;
}

void AmelTechBot::setError(AmelTechError err, const char* status) {
    _lastError = err;
    if (status) {
        strncpy(_lastStatusBuf, status, sizeof(_lastStatusBuf) - 1);
        _lastStatusBuf[sizeof(_lastStatusBuf) - 1] = '\0';
    } else {
        _lastStatusBuf[0] = '\0';
    }
}

String AmelTechBot::normalizeInput(const String& s) const {
    char buf[AMELTECH_MAX_QUESTION_LEN];
    KnowledgeBase::normalize(s.c_str(), buf, sizeof(buf));
    return String(buf);
}

bool AmelTechBot::looksLikeMath(const String& s) const {
    return Calculator::looksLikeExpression(s.c_str());
}

void AmelTechBot::pushContext(const char* q, const char* a, const char* topic) {
    if (_contextSize == 0) return;
    ContextItem& item = _context[_contextHead % _contextSize];
    strncpy(item.question, q ? q : "", sizeof(item.question) - 1);
    item.question[sizeof(item.question) - 1] = '\0';
    strncpy(item.answer, a ? a : "", sizeof(item.answer) - 1);
    item.answer[sizeof(item.answer) - 1] = '\0';
    strncpy(item.topic, topic ? topic : "", sizeof(item.topic) - 1);
    item.topic[sizeof(item.topic) - 1] = '\0';
    item.valid = true;
    _contextHead = (_contextHead + 1) % (_contextSize ? _contextSize : 1);
}

String AmelTechBot::maybeTroll(const String& answer, const String& category) {
    if (!_trolling) return answer;
    // Never replace critical content; only append light humor
    if (category == "esp32" || category == "hardware") {
        return answer + " (Your silicon is still awake, by the way.)";
    }
    if (category == "networking") {
        return answer + " Packets travel faster than excuses.";
    }
    if (category == "science") {
        return answer + " Science: peer-reviewed and caffeine-approved.";
    }
    return answer + " 🤖";
}

String AmelTechBot::handleFollowUp(const String& normalized) {
    // Very light context: pronouns / "that" referring to last topic
    if (_contextSize == 0) return String();
    bool refers = false;
    if (normalized.indexOf("is that") >= 0 || normalized.indexOf("was that") >= 0 ||
        normalized.indexOf("that good") >= 0 || normalized.indexOf("that bad") >= 0 ||
        normalized.indexOf("what about that") >= 0 || normalized == "why" ||
        normalized.indexOf("is it good") >= 0 || normalized.indexOf("is it bad") >= 0) {
        refers = true;
    }
    if (!refers) return String();

    // Find most recent valid context
    for (int i = 0; i < _contextSize; ++i) {
        int idx = (_contextHead + _contextSize - 1 - i) % _contextSize;
        if (!_context[idx].valid) continue;
        String topic = _context[idx].topic;
        String prevA = _context[idx].answer;

        if (topic == "rssi" || prevA.indexOf("RSSI") >= 0 || prevA.indexOf("dBm") >= 0) {
            // Interpret signal quality from last RSSI answer if possible
            int dbmPos = prevA.indexOf("dBm");
            if (dbmPos > 0) {
                // try parse number before dBm
                int start = dbmPos - 1;
                while (start > 0 && (isdigit(prevA[start]) || prevA[start] == '-' || prevA[start] == ' ')) --start;
                String num = prevA.substring(start, dbmPos);
                num.trim();
                int rssi = num.toInt();
                if (rssi < 0) {
                    String q;
                    if (rssi >= -55) q = "That RSSI is excellent.";
                    else if (rssi >= -65) q = "That RSSI is good for most applications.";
                    else if (rssi >= -72) q = "That RSSI is fair; you may see occasional issues.";
                    else if (rssi >= -80) q = "That RSSI is weak; consider moving closer to the AP or reducing interference.";
                    else q = "That RSSI is very weak and likely unreliable.";
                    setError(AMELTECH_OK, "Context follow-up");
                    _lastConfidence = 0.85f;
                    return q;
                }
            }
            return String("I was referring to the previous signal measurement, but I cannot re-evaluate it precisely from context alone.");
        }
        if (topic == "heap" || prevA.indexOf("heap") >= 0) {
            return String("Regarding the previous memory reading: free heap above ~32 KB is generally comfortable for many sketches; lower values deserve attention.");
        }
        if (topic == "health") {
            return String("The previous health score was based on measured telemetry at that moment. Re-run diagnostics for a fresh assessment.");
        }
        // Generic
        return String("Referring to the previous answer: ") + prevA;
    }
    return String();
}

String AmelTechBot::handleHardwareQuery(const String& normalized) {
    // Intent: hardware / telemetry questions
    bool wantRssi = (normalized.indexOf("rssi") >= 0 ||
                     (normalized.indexOf("wifi") >= 0 && normalized.indexOf("signal") >= 0) ||
                     normalized.indexOf("signal strength") >= 0);
    bool wantHeap = (normalized.indexOf("free heap") >= 0 || normalized.indexOf("heap memory") >= 0 ||
                     (normalized.indexOf("memory") >= 0 && normalized.indexOf("free") >= 0));
    bool wantCpu = (normalized.indexOf("cpu") >= 0 && (normalized.indexOf("freq") >= 0 || normalized.indexOf("speed") >= 0));
    bool wantUptime = (normalized.indexOf("uptime") >= 0);
    bool wantTemp = (normalized.indexOf("temperature") >= 0 || normalized.indexOf("temp sensor") >= 0);
    bool wantChip = (normalized.indexOf("chip model") >= 0 || normalized.indexOf("what chip") >= 0 ||
                     normalized.indexOf("esp32 model") >= 0);
    bool wantHealth = (normalized.indexOf("health") >= 0);
    bool wantDiag = (normalized.indexOf("diagnostic") >= 0 || normalized.indexOf("run diag") >= 0);
    bool wantWifiStatus = (normalized.indexOf("wifi") >= 0 &&
                           (normalized.indexOf("status") >= 0 || normalized.indexOf("connected") >= 0));

    if (!wantRssi && !wantHeap && !wantCpu && !wantUptime && !wantTemp &&
        !wantChip && !wantHealth && !wantDiag && !wantWifiStatus) {
        return String();
    }

    _telem.updateFast();
    const ESP32Telemetry& t = _telem.data();
    _lastMeasStatus = MEAS_LIVE;

    if (wantDiag) {
        String r = _diag.run(true);
        pushContext("diagnostics", r.c_str(), "health");
        setError(AMELTECH_OK, "Diagnostics");
        _lastConfidence = 1.0f;
        return r;
    }
    if (wantHealth) {
        String r = _diag.healthReportString();
        pushContext("health", r.c_str(), "health");
        setError(AMELTECH_OK, "Health report");
        _lastConfidence = 1.0f;
        return r;
    }
    if (wantRssi) {
        if (t.wifiRssi.status == MEAS_LIVE || t.wifiRssi.status == MEAS_CACHED) {
            String r = "Wi-Fi RSSI is ";
            r += String(t.wifiRssi.value);
            r += " dBm.";
            if (t.wifiRssi.value < -70) {
                r += " This is a weak signal.";
            }
            pushContext("rssi", r.c_str(), "rssi");
            setError(AMELTECH_OK, "RSSI live");
            _lastConfidence = 1.0f;
            return maybeTroll(r, "networking");
        }
        setError(AMELTECH_UNAVAILABLE, "RSSI unavailable");
        _lastConfidence = 0.0f;
        _lastMeasStatus = t.wifiRssi.status;
        return String("Wi-Fi RSSI is UNAVAILABLE (not connected or measurement not possible).");
    }
    if (wantWifiStatus) {
        if (t.wifiConnected.status == MEAS_LIVE) {
            String r = t.wifiConnected.value ? "Wi-Fi is connected." : "Wi-Fi is not connected.";
            if (t.wifiConnected.value && t.wifiSsidStatus == MEAS_LIVE) {
                r += " SSID: ";
                r += t.wifiSsid;
            }
            pushContext("wifi", r.c_str(), "wifi");
            setError(AMELTECH_OK, "Wi-Fi status");
            _lastConfidence = 1.0f;
            return r;
        }
        return String("Wi-Fi status is UNSUPPORTED or UNAVAILABLE on this build.");
    }
    if (wantHeap) {
        if (t.freeHeap.status == MEAS_LIVE) {
            String r = "Free heap is ";
            r += String(t.freeHeap.value);
            r += " bytes.";
            pushContext("heap", r.c_str(), "heap");
            setError(AMELTECH_OK, "Heap live");
            _lastConfidence = 1.0f;
            return r;
        }
        return String("Free heap measurement is UNAVAILABLE.");
    }
    if (wantCpu) {
        if (t.cpuFreqMhz.status == MEAS_LIVE) {
            String r = "CPU frequency is ";
            r += String(t.cpuFreqMhz.value);
            r += " MHz.";
            pushContext("cpu", r.c_str(), "cpu");
            setError(AMELTECH_OK, "CPU live");
            _lastConfidence = 1.0f;
            return r;
        }
        return String("CPU frequency is UNSUPPORTED or UNAVAILABLE.");
    }
    if (wantUptime) {
        if (t.uptimeMs.status == MEAS_LIVE) {
            String r = "Uptime is ";
            r += String(t.uptimeMs.value / 1000);
            r += " seconds since boot.";
            pushContext("uptime", r.c_str(), "uptime");
            setError(AMELTECH_OK, "Uptime");
            _lastConfidence = 1.0f;
            return r;
        }
        return String("Uptime is UNAVAILABLE.");
    }
    if (wantTemp) {
        if (t.temperatureC.status == MEAS_LIVE) {
            String r = "Internal temperature reading is ";
            r += String(t.temperatureC.value, 1);
            r += " (limited accuracy, not a calibrated external sensor).";
            pushContext("temp", r.c_str(), "temp");
            setError(AMELTECH_OK, "Temperature");
            _lastConfidence = 0.9f;
            return r;
        }
        setError(AMELTECH_UNSUPPORTED, "Temperature unsupported");
        _lastMeasStatus = MEAS_UNSUPPORTED;
        return String("Internal temperature sensor is UNSUPPORTED on this chip/API.");
    }
    if (wantChip) {
        if (t.chipModelStatus == MEAS_LIVE) {
            String r = "Chip model: ";
            r += t.chipModel;
            if (t.chipCoresStatus == MEAS_LIVE) {
                r += ", cores: ";
                r += String(t.chipCores);
            }
            pushContext("chip", r.c_str(), "chip");
            setError(AMELTECH_OK, "Chip info");
            _lastConfidence = 1.0f;
            return r;
        }
        return String("Chip model is UNAVAILABLE.");
    }
    return String();
}

String AmelTechBot::processIntent(const String& normalized, const String& original) {
    // 1) Calculator
    if (looksLikeMath(original) || looksLikeMath(normalized)) {
        String result = _calc.evaluate(original);
        if (_calc.lastError() == CALC_OK) {
            setError(AMELTECH_OK, "Calculation OK");
            _lastConfidence = 1.0f;
            String out = "Result: ";
            out += result;
            pushContext(original.c_str(), out.c_str(), "calc");
            return out;
        }
        // If it looked like math but failed, report calculator error
        if (Calculator::looksLikeExpression(original.c_str())) {
            setError(AMELTECH_INVALID_INPUT, _calc.lastErrorString());
            _lastConfidence = 0.0f;
            String err = "Calculation error: ";
            err += _calc.lastErrorString();
            return err;
        }
    }

    // 2) Hardware / telemetry intents
    String hw = handleHardwareQuery(normalized);
    if (hw.length() > 0) return hw;

    // 3) Context follow-up
    String fu = handleFollowUp(normalized);
    if (fu.length() > 0) return fu;

    // 4) Knowledge base
    MatchResult m = _kb.findBest(original.c_str(), 0.50f);
    _lastConfidence = m.confidence;

    if (!m.found || m.confidence < 0.50f) {
        setError(AMELTECH_NOT_FOUND, "Unknown / insufficient evidence");
        return String("I don't have enough reliable information to answer that.");
    }
    if (m.confidence < 0.75f) {
        setError(AMELTECH_LOW_CONFIDENCE, "Low confidence — clarification preferred");
        String clar = "I'm not fully confident. Did you mean something like: \"";
        // We don't store alternate questions easily; give soft answer with caveat
        clar = "I don't have a high-confidence match. Closest interpretation may be incomplete. ";
        if (m.answer) {
#if defined(ESP32)
            clar += String(m.answer);  // PROGMEM-safe via String ctor on ESP32
#else
            clar += m.answer;
#endif
        }
        return clar;
    }

    String answer;
#if defined(ESP32)
    answer = String(m.answer);
#else
    answer = m.answer ? m.answer : "";
#endif
    String cat = m.category ? String(m.category) : String("general");
    setError(AMELTECH_OK, m.confidence >= 0.90f ? "Strong match" : "Moderate match");
    pushContext(original.c_str(), answer.c_str(), cat.c_str());
    return maybeTroll(answer, cat);
}

String AmelTechBot::ask(const char* question) {
    if (!_ready) {
        setError(AMELTECH_INVALID_CONFIGURATION, "Call begin() first");
        return String("Library not initialized. Call begin() first.");
    }
    if (!question || !question[0]) {
        setError(AMELTECH_INVALID_INPUT, "Empty question");
        _lastConfidence = 0.0f;
        return String("Please provide a non-empty question.");
    }
    if (strlen(question) >= AMELTECH_MAX_QUESTION_LEN) {
        setError(AMELTECH_OVERFLOW, "Question too long");
        return String("Question exceeds maximum length.");
    }

    String original(question);
    String normalized = normalizeInput(original);
    return processIntent(normalized, original);
}

String AmelTechBot::ask(const String& question) {
    return ask(question.c_str());
}

AmelTechError AmelTechBot::train(const String& question, const String& answer, const String& category) {
    return addQA(question, answer, category);
}

AmelTechError AmelTechBot::addQA(const String& question, const String& answer, const String& category) {
    int8_t rc = _kb.addUser(question.c_str(), answer.c_str(), category.c_str());
    switch (rc) {
        case 0:
            setError(AMELTECH_OK, "Trained");
            return AMELTECH_OK;
        case -1:
            setError(AMELTECH_INVALID_INPUT, "Empty question or answer");
            return AMELTECH_INVALID_INPUT;
        case -2:
            setError(AMELTECH_DUPLICATE, "Duplicate question");
            return AMELTECH_DUPLICATE;
        case -3:
            setError(AMELTECH_CONFLICT, "Conflicting answer for similar question");
            return AMELTECH_CONFLICT;
        case -4:
            setError(AMELTECH_MEMORY_ERROR, "User knowledge full");
            return AMELTECH_MEMORY_ERROR;
        case -5:
            setError(AMELTECH_OVERFLOW, "Entry too large");
            return AMELTECH_OVERFLOW;
        default:
            setError(AMELTECH_INVALID_INPUT, "Train failed");
            return AMELTECH_INVALID_INPUT;
    }
}

AmelTechError AmelTechBot::removeQA(const String& question) {
    int8_t rc = _kb.removeUser(question.c_str());
    if (rc == 0) {
        setError(AMELTECH_OK, "Removed");
        return AMELTECH_OK;
    }
    setError(AMELTECH_NOT_FOUND, "Question not found in user knowledge");
    return AMELTECH_NOT_FOUND;
}

void AmelTechBot::clearKnowledge() {
    _kb.clearUser();
    setError(AMELTECH_OK, "User knowledge cleared");
}

size_t AmelTechBot::getKnowledgeCount() const {
    return _kb.totalCount();
}

size_t AmelTechBot::getBuiltinCount() const {
    return _kb.builtinCount();
}

size_t AmelTechBot::getUserCount() const {
    return _kb.userCount();
}

AmelTechError AmelTechBot::saveKnowledge() {
    int8_t rc = _kb.saveToNvs();
    if (rc == 0) {
        setError(AMELTECH_OK, "Saved");
        return AMELTECH_OK;
    }
    setError(AMELTECH_STORAGE_ERROR, "Save failed or NVS unavailable");
    return AMELTECH_STORAGE_ERROR;
}

AmelTechError AmelTechBot::loadKnowledge() {
    int8_t rc = _kb.loadFromNvs();
    if (rc == 0) {
        setError(AMELTECH_OK, "Loaded");
        return AMELTECH_OK;
    }
    setError(AMELTECH_STORAGE_ERROR, "Load failed or NVS unavailable");
    return AMELTECH_STORAGE_ERROR;
}

String AmelTechBot::calculate(const String& expression) {
    return calculate(expression.c_str());
}

String AmelTechBot::calculate(const char* expression) {
    String r = _calc.evaluate(expression);
    if (_calc.lastError() == CALC_OK) {
        setError(AMELTECH_OK, "OK");
        _lastConfidence = 1.0f;
        return r;
    }
    setError(AMELTECH_INVALID_INPUT, _calc.lastErrorString());
    _lastConfidence = 0.0f;
    return String("");
}

void AmelTechBot::resetContext() {
    memset(_context, 0, sizeof(_context));
    _contextHead = 0;
    setError(AMELTECH_OK, "Context reset");
}

void AmelTechBot::setContextSize(uint8_t size) {
    if (size > MAX_CONTEXT) size = MAX_CONTEXT;
    _contextSize = size;
    resetContext();
}

uint8_t AmelTechBot::getContextSize() const {
    return _contextSize;
}

void AmelTechBot::enableTrolling(bool enable) {
    _trolling = enable;
}

bool AmelTechBot::isTrollingEnabled() const {
    return _trolling;
}

AmelTechError AmelTechBot::getLastError() const {
    return _lastError;
}

const char* AmelTechBot::getLastStatus() const {
    return _lastStatusBuf;
}

float AmelTechBot::getConfidence() const {
    return _lastConfidence;
}

MeasurementStatus AmelTechBot::getMeasurementStatus() const {
    return _lastMeasStatus;
}

const ESP32Telemetry& AmelTechBot::getTelemetry(bool full) {
    if (full) _telem.updateFull();
    else _telem.updateFast();
    _lastMeasStatus = _telem.lastStatus();
    return _telem.data();
}

String AmelTechBot::runDiagnostics(bool full) {
    return _diag.run(full);
}

String AmelTechBot::getHealthReport() {
    _telem.updateFast();
    return _diag.healthReportString();
}
