#include "AmelTechBot.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
AmelTechBot::AmelTechBot()
    : _contextSize(AMELTECH_MAX_CONTEXT),
      _contextCount(0),
      _ready(false),
      _trolling(true),
      _lastError(AMELTECH_OK),
      _lastConfidence(0.0f),
      _lastMeasurement(MEAS_UNAVAILABLE),
      _turnCount(0),
      _lastTickMs(0),
      _lastSaveMs(0) {
    memset(_context, 0, sizeof(_context));
    strncpy(_botName, "AmelTech bot", sizeof(_botName) - 1);
    _botName[sizeof(_botName) - 1] = '\0';
    _lastStatus[0] = '\0';
    setStatus("not started");
}

AmelTechBot::~AmelTechBot() {
    end();
}

const char* AmelTechBot::errorToString(AmelTechError err) {
    switch (err) {
        case AMELTECH_OK:                    return "OK";
        case AMELTECH_INVALID_INPUT:         return "INVALID_INPUT";
        case AMELTECH_NOT_FOUND:             return "NOT_FOUND";
        case AMELTECH_LOW_CONFIDENCE:        return "LOW_CONFIDENCE";
        case AMELTECH_UNSUPPORTED:           return "UNSUPPORTED";
        case AMELTECH_UNAVAILABLE:           return "UNAVAILABLE";
        case AMELTECH_MEASUREMENT_ERROR:     return "MEASUREMENT_ERROR";
        case AMELTECH_MEMORY_ERROR:          return "MEMORY_ERROR";
        case AMELTECH_STORAGE_ERROR:         return "STORAGE_ERROR";
        case AMELTECH_TIMEOUT:               return "TIMEOUT";
        case AMELTECH_INVALID_CONFIGURATION: return "INVALID_CONFIGURATION";
        case AMELTECH_DUPLICATE:             return "DUPLICATE";
        case AMELTECH_CONFLICT:              return "CONFLICT";
        case AMELTECH_OVERFLOW:              return "OVERFLOW";
        default:                             return "UNKNOWN";
    }
}

void AmelTechBot::setStatus(const char* status) {
    if (!status) status = "";
    strncpy(_lastStatus, status, sizeof(_lastStatus) - 1);
    _lastStatus[sizeof(_lastStatus) - 1] = '\0';
}

void AmelTechBot::setError(AmelTechError err, const char* status) {
    _lastError = err;
    setStatus(status);
    if (err != AMELTECH_OK) {
        _telemetry.noteError();
        AmelTechLogger.log(AMELTECH_LOG_WARN, "%s: %s", errorToString(err), status);
    }
}

// ---------------------------------------------------------------------------
bool AmelTechBot::begin() {
    AmelTechLogger.begin();

    if (!_kb.begin()) {
        setError(AMELTECH_MEMORY_ERROR, "knowledge base failed to start");
        return false;
    }
    _kb.loadFromNvs();

    _telemetry.begin();
    _thermal.begin(&_telemetry);
    _diag.begin(&_telemetry, &_thermal, &_sensors);
    _console.begin(&_kb);

    _profiles.begin();
    _identity.begin(&_profiles);

    _contextCount = 0;
    memset(_context, 0, sizeof(_context));

    _ready = true;
    _turnCount = 0;
    _lastTickMs = millis();
    _lastSaveMs = millis();
    setError(AMELTECH_OK, "ready");

    AmelTechLogger.log(AMELTECH_LOG_INFO, "AmelTech bot %s ready, %u entries, %u names",
                       AMELTECH_VERSION_STRING,
                       (unsigned)_kb.totalCount(),
                       (unsigned)_profiles.count());
    return true;
}

void AmelTechBot::end() {
    if (!_ready) return;
    if (_kb.isDirty()) _kb.saveToNvs();
    if (_profiles.isDirty()) _profiles.save();
    _sensors.endDht();
    _kb.end();
    _ready = false;
    setStatus("stopped");
}

void AmelTechBot::tick() {
    if (!_ready) return;

    uint32_t now = millis();
    uint32_t delta = now - _lastTickMs;
    _lastTickMs = now;
    if (delta > 0 && delta < 60000) _telemetry.noteLoopLatency(delta);

    // Each subsystem rate limits itself, so this is cheap.
    _telemetry.updateFast(false);
    _thermal.update();

    if (_sensors.isConfigured()) {
        DhtReadStatus rc = _sensors.read(false);
        if (rc == DHT_OK) {
            const DhtReading& r = _sensors.reading();
            _telemetry.setAmbient(r.temperatureC, r.humidityPercent, r.status);
        }
    }

    // Deferred, throttled persistence: never write to flash on every change.
    if (now - _lastSaveMs > AMELTECH_AUTOSAVE_INTERVAL_MS) {
        _lastSaveMs = now;
        if (_kb.isDirty()) _kb.saveToNvs();
        if (_profiles.isDirty()) _profiles.save();
    }
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------
void AmelTechBot::pushContext(const char* question, const char* topic, const char* category) {
    if (_contextSize == 0) return;

    // Shift down; the array is tiny so a memmove is cheaper than a ring index.
    for (int i = (int)_contextSize - 1; i > 0; --i) {
        _context[i] = _context[i - 1];
    }
    ContextEntry& e = _context[0];
    memset(&e, 0, sizeof(e));
    AmelTechText::copyTrimmed(question ? question : "", e.question, sizeof(e.question));
    AmelTechText::copyTrimmed(topic ? topic : "", e.topic, sizeof(e.topic));
    strncpy(e.category, category ? category : "", sizeof(e.category) - 1);
    e.timestamp = millis();
    e.used = true;

    if (_contextCount < _contextSize) ++_contextCount;
}

const ContextEntry* AmelTechBot::lastContext() const {
    if (_contextCount == 0 || !_context[0].used) return nullptr;
    return &_context[0];
}

void AmelTechBot::resetContext() {
    memset(_context, 0, sizeof(_context));
    _contextCount = 0;
}

void AmelTechBot::setContextSize(uint8_t size) {
    if (size > AMELTECH_MAX_CONTEXT) size = AMELTECH_MAX_CONTEXT;
    _contextSize = size;
    if (_contextCount > _contextSize) _contextCount = _contextSize;
}

uint8_t AmelTechBot::getContextSize() const { return _contextSize; }

// ---------------------------------------------------------------------------
// Knowledge
// ---------------------------------------------------------------------------
static AmelTechError kbResultToError(int8_t rc) {
    switch (rc) {
        case KB_ADD_OK:         return AMELTECH_OK;
        case KB_ADD_INVALID:    return AMELTECH_INVALID_INPUT;
        case KB_ADD_DUPLICATE:  return AMELTECH_DUPLICATE;
        case KB_ADD_CONFLICT:   return AMELTECH_CONFLICT;
        case KB_ADD_FULL:       return AMELTECH_OVERFLOW;
        case KB_ADD_TOO_LARGE:  return AMELTECH_OVERFLOW;
        case KB_ADD_NO_MEMORY:  return AMELTECH_MEMORY_ERROR;
        case KB_ADD_HEAP_GUARD: return AMELTECH_MEMORY_ERROR;
        default:                return AMELTECH_UNAVAILABLE;
    }
}

AmelTechError AmelTechBot::addQA(const String& question, const String& answer,
                                 const String& category) {
    if (!_ready) {
        setError(AMELTECH_UNAVAILABLE, "call begin() first");
        return _lastError;
    }
    uint16_t code = 0;
    int8_t rc = _kb.addUser(question.c_str(), answer.c_str(), category.c_str(), &code);
    AmelTechError err = kbResultToError(rc);
    if (err == AMELTECH_OK || err == AMELTECH_CONFLICT) {
        char msg[AMELTECH_STATUS_LEN];
        snprintf(msg, sizeof(msg), "stored as code %04u", (unsigned)code);
        setError(AMELTECH_OK, msg);
        return AMELTECH_OK;
    }
    setError(err, "training rejected");
    return err;
}

AmelTechError AmelTechBot::train(const String& question, const String& answer,
                                 const String& category) {
    return addQA(question, answer, category);
}

AmelTechError AmelTechBot::removeQA(const String& question) {
    if (!_ready) return AMELTECH_UNAVAILABLE;
    int8_t rc = _kb.removeUser(question.c_str());
    if (rc == 0) {
        setError(AMELTECH_OK, "entry removed");
        return AMELTECH_OK;
    }
    setError(AMELTECH_NOT_FOUND, "no taught entry matched");
    return AMELTECH_NOT_FOUND;
}

AmelTechError AmelTechBot::removeQAByCode(uint16_t code) {
    if (!_ready) return AMELTECH_UNAVAILABLE;
    int8_t rc = _kb.removeUserByCode(code);
    if (rc == 0) {
        setError(AMELTECH_OK, "entry removed");
        return AMELTECH_OK;
    }
    setError(AMELTECH_NOT_FOUND, "unknown data number code");
    return AMELTECH_NOT_FOUND;
}

void AmelTechBot::clearKnowledge() {
    _kb.clearUser();
    setError(AMELTECH_OK, "taught knowledge cleared");
}

size_t AmelTechBot::getKnowledgeCount() const { return _kb.totalCount(); }
size_t AmelTechBot::getBuiltinCount() const { return _kb.builtinCount(); }
size_t AmelTechBot::getUserCount() const { return _kb.userCount(); }
uint16_t AmelTechBot::getLastTrainCode() const { return _console.lastCode(); }

AmelTechError AmelTechBot::saveKnowledge() {
    int8_t rc = _kb.saveToNvs();
    if (rc == 0) {
        setError(AMELTECH_OK, "knowledge saved");
        return AMELTECH_OK;
    }
    setError(AMELTECH_STORAGE_ERROR, "flash storage unavailable");
    return AMELTECH_STORAGE_ERROR;
}

AmelTechError AmelTechBot::loadKnowledge() {
    int8_t rc = _kb.loadFromNvs();
    if (rc == 0) {
        setError(AMELTECH_OK, "knowledge loaded");
        return AMELTECH_OK;
    }
    setError(AMELTECH_STORAGE_ERROR, "nothing stored in flash");
    return AMELTECH_STORAGE_ERROR;
}

// ---------------------------------------------------------------------------
// Calculator
// ---------------------------------------------------------------------------
String AmelTechBot::calculate(const String& expression) {
    return calculate(expression.c_str());
}

String AmelTechBot::calculate(const char* expression) {
    String out = _calc.evaluate(expression);
    if (_calc.lastError() == CALC_OK) {
        setError(AMELTECH_OK, "calculated");
        _lastConfidence = 1.0f;
    } else {
        setError(AMELTECH_INVALID_INPUT, _calc.lastErrorString());
        _lastConfidence = 0.0f;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------
bool AmelTechBot::beginDHT(uint8_t pin, DhtType type) {
    bool ok = _sensors.beginDht(pin, type);
    if (!ok) {
        setError(AMELTECH_INVALID_CONFIGURATION, "DHT pin or type is not valid");
        return false;
    }
    setError(AMELTECH_OK, "DHT configured");
    return true;
}

void AmelTechBot::endDHT() {
    _sensors.endDht();
    _telemetry.clearAmbient();
}

bool AmelTechBot::readSensors(bool force) {
    if (!_sensors.isConfigured()) {
        setError(AMELTECH_UNAVAILABLE, "no DHT sensor configured");
        return false;
    }
    DhtReadStatus rc = _sensors.read(force);
    if (rc == DHT_OK) {
        const DhtReading& r = _sensors.reading();
        _telemetry.setAmbient(r.temperatureC, r.humidityPercent, r.status);
        setError(AMELTECH_OK, "sensor read");
        return true;
    }
    if (rc == DHT_TOO_SOON) {
        setError(AMELTECH_OK, "using the last reading (rate limited)");
        return _sensors.reading().status == MEAS_LIVE ||
               _sensors.reading().status == MEAS_CACHED;
    }
    setError(AMELTECH_MEASUREMENT_ERROR, SensorHub::resultName(rc));
    return false;
}

const DhtReading& AmelTechBot::getSensorReading() const { return _sensors.reading(); }
bool AmelTechBot::hasSensor() const { return _sensors.isConfigured(); }

String AmelTechBot::getSensorReport() {
    if (!_sensors.isConfigured()) {
        return String(F("No DHT sensor is connected. "
                        "Call beginDHT(pin, DHT_TYPE_22) in setup() to add one."));
    }
    readSensors(false);
    const DhtReading& r = _sensors.reading();

    String s;
    s.reserve(200);
    s += _sensors.typeName();
    s += F(" on GPIO");
    s += (int)_sensors.pin();
    s += F(": ");

    if (r.status == MEAS_LIVE || r.status == MEAS_CACHED) {
        s += String(r.temperatureC, 1);
        s += F(" C, ");
        s += String(r.humidityPercent, 1);
        s += F("% RH");
        if (r.status == MEAS_CACHED) {
            s += F(" (last good reading, ");
            s += (int)(_sensors.ageMs() / 1000);
            s += F(" s old)");
        }
    } else {
        s += F("no valid reading (");
        s += SensorHub::resultName(r.lastResult);
        s += F("). Check wiring and the pull-up resistor.");
    }
    return s;
}

String AmelTechBot::getSituationReport() {
    if (!_sensors.isConfigured()) {
        return String(F("I cannot analyse the room without a sensor. "
                        "Connect a DHT11 or DHT22 and call beginDHT(pin, type)."));
    }
    readSensors(false);
    SituationReport sit = _sensors.analyze();

    if (!sit.valid) {
        setError(AMELTECH_MEASUREMENT_ERROR, "sensor reading unavailable");
        String s;
        s += F("The ");
        s += _sensors.typeName();
        s += F(" is not answering right now (");
        s += SensorHub::resultName(_sensors.reading().lastResult);
        s += F("), so I will not guess the conditions. "
               "Check the data pin, the 3V3 supply and the pull-up.");
        return s;
    }

    String s;
    s.reserve(420);
    s += sit.headline;
    s += '\n';
    s += F("Temperature ");
    s += String(sit.temperatureC, 1);
    s += F(" C, humidity ");
    s += String(sit.humidityPercent, 0);
    s += F("%, feels like ");
    s += String(sit.heatIndexC, 1);
    s += F(" C, dew point ");
    s += String(sit.dewPointC, 1);
    s += F(" C.\n");
    s += F("Comfort: ");
    s += SensorHub::comfortName(sit.comfort);
    s += F(". Temperature is ");
    s += SensorHub::trendName(sit.temperatureTrend);
    s += F(", humidity is ");
    s += SensorHub::trendName(sit.humidityTrend);
    s += F(".\n");
    s += sit.advice;

    if (_trolling) {
        String joke = _sensors.troll(sit);
        if (joke.length()) {
            s += '\n';
            s += joke;
        }
    }
    setError(AMELTECH_OK, "situation analysed");
    return s;
}

// ---------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------
const char* AmelTechBot::getUserName() const { return _identity.activeName(); }
const char* AmelTechBot::getUserField() const { return _identity.activeField(); }

bool AmelTechBot::rememberUser(const String& name, const String& field) {
    int slot = _profiles.addOrTouch(name.c_str(), field.length() ? field.c_str() : nullptr);
    if (slot < 0) {
        setError(AMELTECH_INVALID_INPUT, "that does not look like a name");
        return false;
    }
    setError(AMELTECH_OK, "name remembered");
    return true;
}

bool AmelTechBot::forgetUser(const String& name) {
    bool ok = _profiles.removeByName(name.c_str());
    if (ok) {
        const char* active = _identity.activeName();
        if (active && strcasecmp(active, name.c_str()) == 0) _identity.forgetActive();
        _profiles.save();
    }
    return ok;
}

void AmelTechBot::forgetAllUsers() {
    _profiles.clear();
    _profiles.save();
    _identity.reset();
}

String AmelTechBot::listUsers() const { return _profiles.list(); }
size_t AmelTechBot::getUserProfileCount() const { return _profiles.count(); }

// ---------------------------------------------------------------------------
// Personality and status
// ---------------------------------------------------------------------------
void AmelTechBot::enableTrolling(bool enable) { _trolling = enable; }
bool AmelTechBot::isTrollingEnabled() const { return _trolling; }

void AmelTechBot::setName(const String& botName) {
    AmelTechText::copyTrimmed(botName.c_str(), _botName, sizeof(_botName));
    if (_botName[0] == '\0') strncpy(_botName, "AmelTech bot", sizeof(_botName) - 1);
}

const char* AmelTechBot::getName() const { return _botName; }

AmelTechError AmelTechBot::getLastError() const { return _lastError; }
const char* AmelTechBot::getLastStatus() const { return _lastStatus; }
float AmelTechBot::getConfidence() const { return _lastConfidence; }
MeasurementStatus AmelTechBot::getMeasurementStatus() const { return _lastMeasurement; }
uint32_t AmelTechBot::getLastScanMicros() const { return _kb.lastScanMicros(); }

// ---------------------------------------------------------------------------
// Hardware reporting
// ---------------------------------------------------------------------------
const ESP32Telemetry& AmelTechBot::getTelemetry(bool full) {
    if (full) _telemetry.updateFull(true);
    else _telemetry.updateFast(true);
    if (_sensors.isConfigured()) {
        const DhtReading& r = _sensors.reading();
        if (r.status == MEAS_LIVE || r.status == MEAS_CACHED) {
            _telemetry.setAmbient(r.temperatureC, r.humidityPercent, r.status);
        }
    }
    return _telemetry.data();
}

String AmelTechBot::runDiagnostics(bool full) { return _diag.run(full); }
String AmelTechBot::getHealthReport() { return _diag.healthReportString(); }
int AmelTechBot::getHealthScore() { return _diag.healthScore(); }
String AmelTechBot::getThermalReport() { return _thermal.report(); }

// ===========================================================================
// Conversation pipeline
// ===========================================================================

// True when a normalized string contains `needle` as whole words.
static bool hasPhrase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return false;
    size_t nl = strlen(needle);
    const char* p = haystack;
    while ((p = strstr(p, needle)) != nullptr) {
        bool leftOk = (p == haystack) || (*(p - 1) == ' ');
        char right = *(p + nl);
        bool rightOk = (right == '\0' || right == ' ');
        if (leftOk && rightOk) return true;
        ++p;
    }
    return false;
}

static bool hasAnyPhrase(const char* haystack, const char* const* list) {
    for (int i = 0; list[i]; ++i) {
        if (hasPhrase(haystack, list[i])) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
bool AmelTechBot::looksLikeMath(const String& raw) const {
    char expr[AMELTECH_CALC_MAX_EXPR];
    return Calculator::extractExpression(raw.c_str(), expr, sizeof(expr));
}

// ---------------------------------------------------------------------------
// Small talk
// ---------------------------------------------------------------------------
String AmelTechBot::handleSmallTalk(const AmelTechQuery& q, bool& handled) {
    handled = false;
    const char* n = q.normalized;

    static const char* const GREETINGS[] = {
        "hi", "hii", "hello", "helo", "hey", "hai", "yo", "hola",
        "good morning", "good afternoon", "good evening", "greetings",
        "hi there", "hello there", nullptr
    };
    static const char* const THANKS[] = {
        "thanks", "thank you", "thanku", "thx", "thank u", "many thanks",
        "appreciate it", nullptr
    };
    static const char* const BYES[] = {
        "bye", "goodbye", "good bye", "see you", "see ya", "cya",
        "good night", "later", nullptr
    };
    static const char* const HOWAREYOU[] = {
        "how are you", "how r u", "how are u", "hows it going",
        "how is it going", "how do you do", "you ok", "are you ok",
        "whats up", "what is up", "sup", nullptr
    };

    // Greetings only count when the message is short; "hi what is ohms law"
    // should be answered, not greeted.
    if (q.tokenCount <= 3 && hasAnyPhrase(n, GREETINGS)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s;
        const char* name = _identity.activeName();
        if (name) {
            s = F("Hello ");
            s += name;
            s += F("! What would you like to know?");
        } else {
            s = F("Hello! I am ");
            s += _botName;
            s += F(". Ask me anything, or type: train | help");
        }
        return s;
    }

    if (hasAnyPhrase(n, HOWAREYOU)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s;
        s.reserve(160);
        s += F("Running well.");

        int score = _diag.healthScore();
        if (score > 0) {
            s += F(" System health is ");
            s += score;
            s += F("/100");
            if (_sensors.isConfigured() && _sensors.isFresh(120000)) {
                s += F(" and the room is ");
                s += String(_sensors.reading().temperatureC, 1);
                s += F(" C");
            }
            s += F(".");
        } else if (_sensors.isConfigured() && _sensors.isFresh(120000)) {
            s += F(" The room is ");
            s += String(_sensors.reading().temperatureC, 1);
            s += F(" C.");
        }
        s += F(" How are you?");
        return s;
    }

    if (q.tokenCount <= 4 && hasAnyPhrase(n, THANKS)) {
        handled = true;
        _lastConfidence = 1.0f;
        return String(F("You are welcome. Ask me anything else."));
    }

    if (q.tokenCount <= 4 && hasAnyPhrase(n, BYES)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s = F("Goodbye");
        const char* name = _identity.activeName();
        if (name) {
            s += ' ';
            s += name;
        }
        s += F("! I will remember what you taught me.");
        if (_kb.isDirty()) _kb.saveToNvs();
        if (_profiles.isDirty()) _profiles.save();
        return s;
    }

    return String();
}

// ---------------------------------------------------------------------------
// Questions about the bot itself
// ---------------------------------------------------------------------------
String AmelTechBot::handleSelfQuery(const AmelTechQuery& q, bool& handled) {
    handled = false;
    const char* n = q.normalized;

    static const char* const WHOAREYOU[] = {
        "who are you", "what are you", "your name", "whats your name",
        "what is your name", "who r u", "introduce yourself", nullptr
    };
    static const char* const CAPABILITIES[] = {
        "what can you do", "what do you do", "help", "commands",
        "what can i ask", "how do i use you", "how to use", nullptr
    };
    static const char* const VERSIONQ[] = {
        "your version", "what version", "which version", "version number",
        nullptr
    };
    static const char* const COUNTQ[] = {
        "how many questions do you know", "how much do you know",
        "how many answers", "knowledge count", "how many entries", nullptr
    };

    if (hasAnyPhrase(n, WHOAREYOU)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s;
        s += F("I am ");
        s += _botName;
        s += F(", an offline assistant running on this ESP32. ");
        s += F("Everything I answer comes from knowledge stored on the chip, "
               "so I work with no internet at all.");
        return s;
    }

    if (hasAnyPhrase(n, VERSIONQ)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s = F("AmelTech lab's bot version ");
        s += AMELTECH_VERSION_STRING;
        s += F(".");
        return s;
    }

    if (hasAnyPhrase(n, COUNTQ)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s;
        s += F("I know ");
        s += (int)_kb.builtinCount();
        s += F(" built-in questions");
        if (_kb.userCount()) {
            s += F(" plus ");
            s += (int)_kb.userCount();
            s += F(" you taught me");
        }
        s += F(". I also do full maths and read sensors.");
        return s;
    }

    if (q.tokenCount <= 5 && hasAnyPhrase(n, CAPABILITIES)) {
        handled = true;
        _lastConfidence = 1.0f;
        String s;
        s.reserve(420);
        s += F("Here is what I can do:\n");
        s += F("- Answer general knowledge, science and electronics questions\n");
        s += F("- Solve maths: 25*4, sqrt(144), 15% of 200, 2^10, 7!, sin(pi/2)\n");
        s += F("- Report this board: heap, CPU, Wi-Fi, temperature, health score\n");
        if (_sensors.isConfigured()) {
            s += F("- Read the ");
            s += _sensors.typeName();
            s += F(" and analyse the room conditions\n");
        } else {
            s += F("- Read a DHT11 or DHT22 once you call beginDHT(pin, type)\n");
        }
        s += F("- Remember your name between power cycles\n");
        s += F("- Learn from you: train | question | answer");
        return s;
    }

    return String();
}

// ---------------------------------------------------------------------------
// Sensor and room questions
// ---------------------------------------------------------------------------
String AmelTechBot::handleSensorQuery(const AmelTechQuery& q, bool& handled) {
    handled = false;
    const char* n = q.normalized;

    static const char* const ROOMWORDS[] = {
        "room", "ambient", "here", "inside", "outside", "weather", "air",
        "environment", "surroundings", "situation", nullptr
    };
    static const char* const TEMPWORDS[] = {
        "temperature", "temp", "how hot", "how cold", "how warm", nullptr
    };
    static const char* const HUMWORDS[] = {
        "humidity", "humid", "moisture", "damp", "rh", nullptr
    };
    static const char* const SITUATION[] = {
        "situation", "conditions", "analyse", "analyze", "analysis",
        "how is the room", "how is it", "comfort", "comfortable",
        "dew point", "heat index", "feels like", nullptr
    };

    bool wantsTemp = hasAnyPhrase(n, TEMPWORDS);
    bool wantsHum = hasAnyPhrase(n, HUMWORDS);
    bool wantsSituation = hasAnyPhrase(n, SITUATION);
    bool roomish = hasAnyPhrase(n, ROOMWORDS);

    if (!wantsTemp && !wantsHum && !wantsSituation) return String();

    // "what is temperature" as a definition question belongs to the knowledge
    // base, not the sensor. Only take it when it is about *here* and now.
    if (!roomish && !wantsHum && !wantsSituation) {
        if (hasPhrase(n, "what is temperature") || hasPhrase(n, "what is temp") ||
            hasPhrase(n, "define temperature") || hasPhrase(n, "explain temperature")) {
            return String();
        }
    }

    // A chip temperature question is hardware, not ambient.
    if (hasPhrase(n, "cpu") || hasPhrase(n, "chip") || hasPhrase(n, "core") ||
        hasPhrase(n, "esp32") || hasPhrase(n, "board") || hasPhrase(n, "die")) {
        return String();
    }

    handled = true;

    if (!_sensors.isConfigured()) {
        _lastConfidence = 1.0f;
        _lastMeasurement = MEAS_UNSUPPORTED;
        setError(AMELTECH_UNAVAILABLE, "no DHT sensor configured");
        return String(F("I have no temperature or humidity sensor connected, "
                        "so I will not invent a number. Wire a DHT11 or DHT22 "
                        "to a GPIO and call beginDHT(pin, DHT_TYPE_22) in setup()."));
    }

    readSensors(false);
    const DhtReading& r = _sensors.reading();
    _lastMeasurement = r.status;

    if (r.status != MEAS_LIVE && r.status != MEAS_CACHED) {
        _lastConfidence = 1.0f;
        setError(AMELTECH_MEASUREMENT_ERROR, SensorHub::resultName(r.lastResult));
        String s;
        s += F("The ");
        s += _sensors.typeName();
        s += F(" did not answer (");
        s += SensorHub::resultName(r.lastResult);
        s += F("). I would rather say nothing than make a reading up. "
               "Check the data pin, the 3V3 supply and the pull-up resistor.");
        return s;
    }

    _lastConfidence = 1.0f;
    setError(AMELTECH_OK, "sensor answered");

    if (wantsSituation || (wantsTemp && wantsHum)) {
        return getSituationReport();
    }

    SituationReport sit = _sensors.analyze();
    String s;
    s.reserve(220);

    if (wantsTemp) {
        s += F("It is ");
        s += String(r.temperatureC, 1);
        s += F(" C in the room");
        if (sit.valid) {
            s += F(", which feels like ");
            s += String(sit.heatIndexC, 1);
            s += F(" C at ");
            s += String(r.humidityPercent, 0);
            s += F("% humidity");
        }
        s += F(".");
    } else {
        s += F("Humidity is ");
        s += String(r.humidityPercent, 1);
        s += F("%");
        if (sit.valid) {
            s += F(" at ");
            s += String(r.temperatureC, 1);
            s += F(" C, dew point ");
            s += String(sit.dewPointC, 1);
            s += F(" C");
        }
        s += F(".");
    }

    if (r.status == MEAS_CACHED) {
        s += F(" (last good reading, ");
        s += (int)(_sensors.ageMs() / 1000);
        s += F(" s old)");
    }

    if (sit.valid && sit.advice[0]) {
        s += ' ';
        s += sit.advice;
    }

    if (_trolling && sit.valid) {
        String joke = _sensors.troll(sit);
        if (joke.length()) {
            s += '\n';
            s += joke;
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// Board / hardware questions
// ---------------------------------------------------------------------------
String AmelTechBot::handleHardwareQuery(const AmelTechQuery& q, bool& handled) {
    handled = false;
    const char* n = q.normalized;

    static const char* const MEMWORDS[] = {
        "free heap", "heap", "free memory", "memory", "ram", "free ram",
        "how much memory", "memory left", nullptr
    };
    static const char* const CPUWORDS[] = {
        "cpu frequency", "cpu speed", "clock speed", "how fast is your cpu",
        "cpu freq", "processor speed", nullptr
    };
    static const char* const WIFIWORDS[] = {
        "wifi status", "wifi signal", "signal strength", "rssi", "are you online",
        "wifi connected", "network status", "am i connected", nullptr
    };
    static const char* const UPWORDS[] = {
        "uptime", "how long have you been running", "how long are you running",
        "running time", nullptr
    };
    static const char* const CHIPWORDS[] = {
        "chip model", "which chip", "what chip", "chip info", "board info",
        "what board", "chip revision", "how many cores", nullptr
    };
    static const char* const HEALTHWORDS[] = {
        "health", "health score", "how healthy", "system health",
        "are you healthy", "status report", nullptr
    };
    static const char* const DIAGWORDS[] = {
        "diagnostics", "diagnostic", "run diagnostics", "full report",
        "system report", "self test", nullptr
    };
    static const char* const CHIPTEMP[] = {
        "cpu temperature", "chip temperature", "core temperature",
        "die temperature", "how hot is the chip", "how hot is your cpu",
        "esp32 temperature", "board temperature", nullptr
    };

    if (hasAnyPhrase(n, DIAGWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        return runDiagnostics(true);
    }

    if (hasAnyPhrase(n, HEALTHWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFull(true);
        return _diag.healthReportString();
    }

    if (hasAnyPhrase(n, CHIPTEMP)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFull(true);
        const ESP32Telemetry& t = _telemetry.data();
        _lastMeasurement = t.temperatureC.status;

        if (Telemetry::statusIsUsable(t.temperatureC.status)) {
            String s = F("The die temperature is ");
            s += String(t.temperatureC.value, 1);
            s += F(" C (");
            s += ThermalGuard::stateName(_thermal.state());
            s += F(").");
            return s;
        }

        String s = F("This chip has no usable internal temperature sensor, "
                     "so I cannot give you a real die temperature.");
        if (_sensors.isConfigured() && _sensors.isFresh(120000)) {
            s += F(" Ambient is ");
            s += String(_sensors.reading().temperatureC, 1);
            s += F(" C, and the die usually runs 10-20 C above that.");
        } else {
            s += F(" Connect a DHT sensor and I can at least tell you the ambient.");
        }
        return s;
    }

    if (hasAnyPhrase(n, MEMWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFast(true);
        const ESP32Telemetry& t = _telemetry.data();
        _lastMeasurement = t.freeHeap.status;

        if (!Telemetry::statusIsUsable(t.freeHeap.status)) {
            return String(F("Heap statistics are not available on this build."));
        }
        String s;
        s += F("Free heap: ");
        s += (int)(t.freeHeap.value / 1024);
        s += F(" KB");
        if (Telemetry::statusIsUsable(t.heapSize.status)) {
            s += F(" of ");
            s += (int)(t.heapSize.value / 1024);
            s += F(" KB");
        }
        if (Telemetry::statusIsUsable(t.heapFragmentationPct.status)) {
            s += F(", ");
            s += (int)t.heapFragmentationPct.value;
            s += F("% fragmented");
        }
        s += F(". Taught entries use ");
        s += (int)(_kb.userHeapBytes() / 1024);
        s += F(" KB.");
        return s;
    }

    if (hasAnyPhrase(n, CPUWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFast(true);
        const ESP32Telemetry& t = _telemetry.data();
        _lastMeasurement = t.cpuFreqMhz.status;

        if (!Telemetry::statusIsUsable(t.cpuFreqMhz.status)) {
            return String(F("The CPU frequency is not readable on this build."));
        }
        String s = F("CPU is running at ");
        s += (int)t.cpuFreqMhz.value;
        s += F(" MHz");
        if (_thermal.isThrottling()) s += F(", currently throttled to keep cool");
        s += F(".");
        return s;
    }

    if (hasAnyPhrase(n, WIFIWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFull(true);
        const ESP32Telemetry& t = _telemetry.data();
        _lastMeasurement = t.wifiConnected.status;

        if (!Telemetry::statusIsUsable(t.wifiConnected.status)) {
            return String(F("Wi-Fi is not available in this build. "
                            "Everything I do works offline anyway."));
        }
        if (!t.wifiConnected.value) {
            return String(F("Wi-Fi is not connected. I do not need it: "
                            "all my knowledge is stored on this chip."));
        }
        String s = F("Wi-Fi is connected");
        if (t.wifiSsidStatus == MEAS_LIVE && t.wifiSsid[0]) {
            s += F(" to ");
            s += t.wifiSsid;
        }
        if (Telemetry::statusIsUsable(t.wifiRssi.status)) {
            int rssi = t.wifiRssi.value;
            s += F(", RSSI ");
            s += rssi;
            s += F(" dBm (");
            if (rssi >= -55) s += F("excellent");
            else if (rssi >= -67) s += F("good");
            else if (rssi >= -75) s += F("fair");
            else s += F("weak");
            s += F(")");
        }
        s += F(".");
        return s;
    }

    if (hasAnyPhrase(n, UPWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFast(true);
        const ESP32Telemetry& t = _telemetry.data();
        _lastMeasurement = t.uptimeMs.status;

        if (!Telemetry::statusIsUsable(t.uptimeMs.status)) {
            return String(F("Uptime is not available."));
        }
        uint32_t sec = t.uptimeMs.value / 1000;
        String s = F("I have been running for ");
        if (sec >= 3600) {
            s += (int)(sec / 3600);
            s += F(" h ");
            s += (int)((sec % 3600) / 60);
            s += F(" min");
        } else if (sec >= 60) {
            s += (int)(sec / 60);
            s += F(" min ");
            s += (int)(sec % 60);
            s += F(" s");
        } else {
            s += (int)sec;
            s += F(" s");
        }
        s += F(", and I have answered ");
        s += (int)_turnCount;
        s += F(" questions.");
        return s;
    }

    if (hasAnyPhrase(n, CHIPWORDS)) {
        handled = true;
        _lastConfidence = 1.0f;
        _telemetry.updateFull(true);
        const ESP32Telemetry& t = _telemetry.data();
        _lastMeasurement = t.chipModelStatus;

        String s;
        if (t.chipModelStatus == MEAS_LIVE) {
            s += F("This board uses an ");
            s += t.chipModel;
        } else {
            s += F("Chip model is not reported by this build");
        }
        if (t.chipCoresStatus == MEAS_LIVE) {
            s += F(" with ");
            s += (int)t.chipCores;
            s += (t.chipCores == 1) ? F(" core") : F(" cores");
        }
        if (t.chipRevisionStatus == MEAS_LIVE) {
            s += F(", revision ");
            s += (int)t.chipRevision;
        }
        if (Telemetry::statusIsUsable(t.flashSize.status)) {
            s += F(", ");
            s += (int)(t.flashSize.value / (1024UL * 1024UL));
            s += F(" MB flash");
        }
        s += F(".");
        return s;
    }

    return String();
}

// ---------------------------------------------------------------------------
// Follow-up questions that only make sense with the previous turn
// ---------------------------------------------------------------------------
String AmelTechBot::handleFollowUp(const AmelTechQuery& q, bool& handled) {
    handled = false;
    const char* n = q.normalized;

    static const char* const FOLLOWUPS[] = {
        "tell me more", "more", "more info", "more about that", "go on",
        "continue", "explain more", "explain that", "what else", "and then",
        "why", "why is that", "how so", "elaborate", "details", nullptr
    };

    if (q.tokenCount > 4) return String();
    if (!hasAnyPhrase(n, FOLLOWUPS)) return String();

    const ContextEntry* ctx = lastContext();
    if (!ctx || !ctx->topic[0]) {
        handled = true;
        _lastConfidence = 0.5f;
        return String(F("There is nothing to expand on yet. "
                        "Ask me a question first and I will go deeper."));
    }

    // Re-ask the knowledge base about the remembered topic.
    String probe = F("what is ");
    probe += ctx->topic;

    AmelTechQuery sub;
    NeuralEngine::buildQuery(probe.c_str(), sub);
    MatchResult m = _kb.findBest(sub, AMELTECH_CONF_WEAK);

    handled = true;
    if (m.found && m.answer) {
        _lastConfidence = m.confidence;
        String s = F("More about ");
        s += ctx->topic;
        s += F(": ");
        s += m.answer;
        return s;
    }

    _lastConfidence = 0.4f;
    String s = F("I have told you what I know about ");
    s += ctx->topic;
    s += F(". Ask me something more specific and I will try again.");
    return s;
}

// ---------------------------------------------------------------------------
// Query rewriting
//
// The knowledge base stores questions in a canonical "what is X" form. People
// do not type that way, so when the first attempt is not confident the same
// question is offered again in a few equivalent shapes and the best result is
// kept. This is what turns "tell me about bluetooth" into a strong match.
// ---------------------------------------------------------------------------
bool AmelTechBot::rewriteQuery(const String& raw, uint8_t attempt, String& out) {
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(raw.c_str(), norm, sizeof(norm));
    if (!norm[0]) return false;

    // Leading phrases that add nothing to the meaning.
    static const char* const LEAD_INS[] = {
        "tell me about the ", "tell me about a ", "tell me about ",
        "tell me what is ", "tell me ",
        "can you tell me about ", "can you explain ", "can you tell me ",
        "could you explain ", "please explain ", "please tell me about ",
        "i want to know about ", "i would like to know about ",
        "i want to know ", "do you know about ", "do you know what is ",
        "do you know ", "explain to me ", "explain about ", "explain the ",
        "explain ", "describe the ", "describe ", "define the ", "define ",
        "what do you know about ", "give me info about ",
        "give me information about ", "information about ", "info about ",
        "something about ", "about the ", "talk about ",
        nullptr
    };

    // Find the topic once; the attempts differ only in how it is framed.
    const char* topic = nullptr;
    for (int i = 0; LEAD_INS[i]; ++i) {
        size_t pl = strlen(LEAD_INS[i]);
        if (strncmp(norm, LEAD_INS[i], pl) == 0 && norm[pl] != '\0') {
            topic = norm + pl;
            break;
        }
    }

    if (!topic) {
        // No lead-in. Try dropping a trailing "mean"/"means" and similar.
        if (attempt == 0) {
            static const char* const TAILS[] = {" mean", " means", " meaning", nullptr};
            size_t nl = strlen(norm);
            for (int i = 0; TAILS[i]; ++i) {
                size_t tl = strlen(TAILS[i]);
                if (nl > tl && strcmp(norm + nl - tl, TAILS[i]) == 0) {
                    String base(norm);
                    base.remove(base.length() - tl);
                    // "what does X mean" -> "what is X"
                    if (base.startsWith("what does ")) {
                        out = "what is " + base.substring(10);
                    } else {
                        out = base;
                    }
                    return true;
                }
            }
        }
        if (attempt == 1 && strncmp(norm, "what is ", 8) != 0 &&
            strncmp(norm, "who is ", 7) != 0 && strncmp(norm, "how ", 4) != 0 &&
            strncmp(norm, "why ", 4) != 0 && strncmp(norm, "when ", 5) != 0) {
            // A bare topic such as "bluetooth".
            out = "what is ";
            out += norm;
            return true;
        }
        return false;
    }

    switch (attempt) {
        case 0: out = "what is "; out += topic; return true;
        case 1: out = topic;                    return true;
        case 2: out = "how does "; out += topic; out += " work"; return true;
        default: return false;
    }
}

// ---------------------------------------------------------------------------
String AmelTechBot::tryKnowledge(const String& raw, float& confidenceOut, bool& foundOut,
                                 String& topicOut, String& categoryOut) {
    foundOut = false;
    confidenceOut = 0.0f;

    AmelTechQuery q;
    NeuralEngine::buildQuery(raw.c_str(), q);
    MatchResult best = _kb.findBest(q, 0.0f);
    String bestAnswer = (best.found && best.answer) ? String(best.answer) : String();
    float bestConf = best.found ? best.confidence : 0.0f;
    String bestTopic = (best.found && best.matchedQuestion) ? String(best.matchedQuestion)
                                                            : String();
    String bestCategory = (best.found && best.category) ? String(best.category) : String("general");

    // Retry with canonical phrasings while the answer is not convincing.
    if (bestConf < AMELTECH_CONF_MODERATE) {
        for (uint8_t attempt = 0; attempt < 3; ++attempt) {
            String rewritten;
            if (!rewriteQuery(raw, attempt, rewritten)) continue;
            if (rewritten.length() == 0) continue;

            AmelTechQuery rq;
            NeuralEngine::buildQuery(rewritten.c_str(), rq);
            MatchResult m = _kb.findBest(rq, 0.0f);
            if (m.found && m.confidence > bestConf) {
                bestConf = m.confidence;
                bestAnswer = m.answer ? m.answer : "";
                bestTopic = m.matchedQuestion ? m.matchedQuestion : "";
                bestCategory = m.category ? m.category : "general";
            }
            if (bestConf >= AMELTECH_CONF_STRONG) break;
            AMELTECH_YIELD();
        }
    }

    confidenceOut = bestConf;
    topicOut = bestTopic;
    categoryOut = bestCategory;
    foundOut = (bestConf >= AMELTECH_CONF_WEAK) && bestAnswer.length() > 0;
    return bestAnswer;
}

// ---------------------------------------------------------------------------
String AmelTechBot::buildFallback(const String& raw) {
    // Offer the nearest topics so the answer is still useful.
    AmelTechQuery q;
    NeuralEngine::buildQuery(raw.c_str(), q);

    MatchResult ranked[3];
    uint8_t n = _kb.rank(q, ranked, 3);

    String s;
    s.reserve(240);
    s += F("I do not have a reliable answer for that, and I will not invent one.");

    if (n > 0 && ranked[0].found && ranked[0].confidence > 0.30f) {
        s += F(" Did you mean:");
        for (uint8_t i = 0; i < n; ++i) {
            if (!ranked[i].found || !ranked[i].matchedQuestion) continue;
            if (ranked[i].confidence < 0.30f) continue;
            s += F("\n  - ");
            s += ranked[i].matchedQuestion;
        }
    } else {
        s += F(" You can teach me the answer:\n  train | ");
        s += raw;
        s += F(" | your answer here");
    }
    return s;
}

// ---------------------------------------------------------------------------
String AmelTechBot::maybeTroll(const String& answer, const char* category) {
    if (!_trolling) return answer;
    if (!category) return answer;

    // Humour is only ever added to sensor answers, where it is a comment on a
    // real measurement. Facts are never dressed up.
    if (strcmp(category, "sensor") != 0) return answer;

    SituationReport sit = _sensors.analyze();
    if (!sit.valid) return answer;
    String joke = _sensors.troll(sit);
    if (!joke.length()) return answer;

    String s = answer;
    s += '\n';
    s += joke;
    return s;
}

// ---------------------------------------------------------------------------
String AmelTechBot::decorateWithName(const String& reply) {
    const char* name = _identity.activeName();
    if (!name || !name[0]) return reply;
    if (!_identity.shouldMentionName()) return reply;

    // Do not append a name to something that already contains it.
    if (reply.indexOf(name) >= 0) return reply;
    // Multi-line reports read badly with a name tacked on.
    if (reply.indexOf('\n') >= 0) return reply;
    if (reply.length() > 220) return reply;

    String s = reply;
    // Insert before the final punctuation when there is one.
    if (s.length() && (s[s.length() - 1] == '.' || s[s.length() - 1] == '!')) {
        char last = s[s.length() - 1];
        s.remove(s.length() - 1);
        s += F(", ");
        s += name;
        s += last;
    } else {
        s += F(" (");
        s += name;
        s += F(")");
    }
    return s;
}

// ===========================================================================
// Entry points
// ===========================================================================
String AmelTechBot::answerQuestion(const String& raw, bool allowIdentity) {
    _thermal.beginSlice();

    if (!_ready) {
        setError(AMELTECH_UNAVAILABLE, "call begin() first");
        return String(F("I am not started yet. Call bot.begin() in setup()."));
    }

    String text = raw;
    text.trim();

    if (text.length() == 0) {
        setError(AMELTECH_INVALID_INPUT, "empty question");
        return String(F("Ask me something and I will answer."));
    }

    if (text.length() >= AMELTECH_MAX_QUESTION_LEN) {
        setError(AMELTECH_OVERFLOW, "question too long");
        String s = F("That is longer than I can process (");
        s += (int)(AMELTECH_MAX_QUESTION_LEN - 1);
        s += F(" characters maximum). Please ask it in a shorter way.");
        return s;
    }

    ++_turnCount;
    _lastMeasurement = MEAS_UNAVAILABLE;
    _lastConfidence = 0.0f;

    String identityPrefix;
    String working = text;

    // ---- identity ------------------------------------------------------
    if (allowIdentity) {
        String idReply, idQuestion;
        IdentityAction act = _identity.process(text, idReply, idQuestion);

        if (act == ID_ACTION_REPLY) {
            setError(AMELTECH_OK, "identity");
            _lastConfidence = 1.0f;
            _identity.noteReply();
            _thermal.tick();
            return idReply;
        }
        if (act == ID_ACTION_CONTINUE_WITH) {
            identityPrefix = idReply;
            idQuestion.trim();
            if (idQuestion.length() > 0 && idQuestion.length() < AMELTECH_MAX_QUESTION_LEN) {
                working = idQuestion;
            } else {
                setError(AMELTECH_OK, "identity");
                _lastConfidence = 1.0f;
                _identity.noteReply();
                _thermal.tick();
                return idReply;
            }
        }
    }

    AmelTechQuery q;
    NeuralEngine::buildQuery(working.c_str(), q);

    String answer;
    bool handled = false;
    const char* category = "general";
    String topic;

    // ---- fast intents ---------------------------------------------------
    if (!handled) {
        answer = handleSelfQuery(q, handled);
        if (handled) category = "self";
    }
    if (!handled) {
        answer = handleSmallTalk(q, handled);
        if (handled) category = "smalltalk";
    }
    AMELTECH_YIELD();

    // ---- maths ----------------------------------------------------------
    if (!handled) {
        char expr[AMELTECH_CALC_MAX_EXPR];
        if (Calculator::extractExpression(working.c_str(), expr, sizeof(expr))) {
            double value = 0.0;
            if (_calc.evaluateTo(expr, value)) {
                handled = true;
                category = "math";
                _lastConfidence = 1.0f;
                setError(AMELTECH_OK, "calculated");
                answer = Calculator::formatNumber(value, _calc.precision());
            } else if (_calc.lastError() != CALC_EMPTY) {
                // It clearly was maths, it just did not work out.
                handled = true;
                category = "math";
                _lastConfidence = 1.0f;
                setError(AMELTECH_INVALID_INPUT, _calc.lastErrorString());
                answer = F("I could not calculate that: ");
                answer += _calc.lastErrorString();
                answer += F(".");
            }
        }
    }
    AMELTECH_YIELD();

    // ---- sensors and hardware -------------------------------------------
    if (!handled) {
        answer = handleSensorQuery(q, handled);
        if (handled) category = "sensor";
    }
    if (!handled) {
        answer = handleHardwareQuery(q, handled);
        if (handled) category = "hardware";
    }
    AMELTECH_YIELD();

    // ---- follow-up ------------------------------------------------------
    if (!handled) {
        answer = handleFollowUp(q, handled);
        if (handled) category = "followup";
    }

    // ---- knowledge base --------------------------------------------------
    if (!handled) {
        float conf = 0.0f;
        bool found = false;
        String kbTopic, kbCategory;
        String kbAnswer = tryKnowledge(working, conf, found, kbTopic, kbCategory);

        _lastConfidence = conf;
        topic = kbTopic;

        if (found && conf >= AMELTECH_CONF_MODERATE) {
            handled = true;
            answer = kbAnswer;
            category = "knowledge";
            setError(AMELTECH_OK, "answered from knowledge");
        } else if (found && conf >= AMELTECH_CONF_WEAK) {
            handled = true;
            category = "knowledge";
            setError(AMELTECH_LOW_CONFIDENCE, "answered with a hedge");
            answer = F("I think you are asking about ");
            answer += kbTopic;
            answer += F(". ");
            answer += kbAnswer;
        } else {
            handled = true;
            category = "unknown";
            setError(AMELTECH_NOT_FOUND, "no confident match");
            answer = buildFallback(working);
        }
    }

    // ---- post processing -------------------------------------------------
    answer = maybeTroll(answer, category);
    answer = decorateWithName(answer);

    if (identityPrefix.length()) {
        String s = identityPrefix;
        s += ' ';
        s += answer;
        answer = s;
    }

    if (strcmp(category, "unknown") != 0) {
        // The stored topic is the subject alone, not the whole matched
        // question, so a follow-up reads as "More about water" rather than
        // "More about what is water".
        String subject = topic.length() ? topic : working;
        static const char* const SUBJECT_LEAD_INS[] = {
            "what is a ", "what is an ", "what is the ", "what is ",
            "what are the ", "what are ", "who is the ", "who is ",
            "how does a ", "how does the ", "how does ", "how do ",
            "why is the ", "why is ", "why does ", "define ", "explain ",
            nullptr
        };
        String lowered = subject;
        lowered.toLowerCase();
        for (int i = 0; SUBJECT_LEAD_INS[i]; ++i) {
            size_t pl = strlen(SUBJECT_LEAD_INS[i]);
            if (lowered.startsWith(SUBJECT_LEAD_INS[i]) && subject.length() > pl) {
                subject = subject.substring(pl);
                break;
            }
        }
        subject.trim();
        if (subject.length() == 0) subject = working;

        pushContext(working.c_str(), subject.c_str(), category);
    }

    _identity.noteReply();
    _thermal.tick();
    return answer;
}

String AmelTechBot::ask(const String& question) {
    return answerQuestion(question, true);
}

String AmelTechBot::ask(const char* question) {
    return answerQuestion(String(question ? question : ""), true);
}

// ---------------------------------------------------------------------------
String AmelTechBot::handleSerialLine(const String& line) {
    String text = line;
    text.trim();
    if (text.length() == 0) return String();

    if (TrainingConsole::isTrainingCommand(text.c_str())) {
        if (!_ready) return String(F("Call bot.begin() in setup() before training."));
        String result = _console.handle(text);
        // Training changes memory pressure; refresh the picture cheaply.
        _telemetry.updateFast(false);
        return result;
    }

    // A few maintenance commands that are useful from the Serial Monitor.
    String lower = text;
    lower.toLowerCase();

    if (lower == "help" || lower == "?") {
        String s;
        s += F("Type a question, or use:\n");
        s += TrainingConsole::helpText();
        s += F("\nAlso: diagnostics, health, names, forget me, version");
        return s;
    }
    if (lower == "names" || lower == "list names") {
        return _profiles.list();
    }
    if (lower == "forget me") {
        const char* n = _identity.activeName();
        if (!n) return String(F("I do not know who you are yet."));
        String name(n);
        _profiles.removeByName(name.c_str());
        _identity.forgetActive();
        _profiles.save();
        String s = F("Forgotten. I no longer remember ");
        s += name;
        s += F(".");
        return s;
    }

    return ask(text);
}

bool AmelTechBot::pollSerial(Stream& stream, String& out) {
    while (stream.available()) {
        char c = (char)stream.read();
        if (c == '\r') continue;
        if (c == '\n') {
            String line = _serialBuffer;
            _serialBuffer = "";
            line.trim();
            if (line.length() == 0) return false;
            out = handleSerialLine(line);
            return true;
        }
        if (_serialBuffer.length() < AMELTECH_MAX_QUESTION_LEN + AMELTECH_MAX_ANSWER_LEN) {
            _serialBuffer += c;
        }
    }
    return false;
}
