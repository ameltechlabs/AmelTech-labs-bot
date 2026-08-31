/*
 * AmelTech lab's bot  -  AmelTechBot.h
 * ===========================================================================
 * Offline chatbot library for the ESP32 family.
 *
 *   Deterministic neural-style matcher  +  full expression calculator
 *   +  DHT11 / DHT22 sensing with situation analysis
 *   +  name memory that survives a power cycle
 *   +  serial training console  +  weighted health diagnostics
 *
 * A sketch normally needs only this one header:
 *
 *     #include <AmelTechBot.h>
 *     AmelTechBot bot;
 *
 *     void setup() {
 *         Serial.begin(115200);
 *         bot.begin();
 *         bot.beginDHT(4, DHT_TYPE_22);   // optional
 *     }
 *
 *     void loop() {
 *         if (Serial.available()) {
 *             String line = Serial.readStringUntil('\n');
 *             Serial.println(bot.handleSerialLine(line));
 *         }
 *         bot.tick();
 *     }
 *
 * Everything runs on the device. No network, no cloud, no API key.
 *
 * A note on "neural": the matcher uses fixed, deterministic feature hashing
 * and a blended similarity score. It behaves like a small language model in
 * the sense that it generalises over wording, but it is not a trained network
 * and it never invents facts. The same question always gives the same answer.
 * ===========================================================================
 */

#ifndef AMELTECH_BOT_H
#define AMELTECH_BOT_H

#include <Arduino.h>
#include "AmelTechConfig.h"
#include "AmelTechLog.h"
#include "NeuralEngine.h"
#include "KnowledgeBase.h"
#include "Calculator.h"
#include "SensorHub.h"
#include "Telemetry.h"
#include "ThermalGuard.h"
#include "Diagnostics.h"
#include "UserProfile.h"
#include "TrainingConsole.h"

// ---------------------------------------------------------------------------
// Error / status codes (unchanged from v1 so old sketches keep compiling)
// ---------------------------------------------------------------------------
enum AmelTechError : int8_t {
    AMELTECH_OK = 0,
    AMELTECH_INVALID_INPUT = -1,
    AMELTECH_NOT_FOUND = -2,
    AMELTECH_LOW_CONFIDENCE = -3,
    AMELTECH_UNSUPPORTED = -4,
    AMELTECH_UNAVAILABLE = -5,
    AMELTECH_MEASUREMENT_ERROR = -6,
    AMELTECH_MEMORY_ERROR = -7,
    AMELTECH_STORAGE_ERROR = -8,
    AMELTECH_TIMEOUT = -9,
    AMELTECH_INVALID_CONFIGURATION = -10,
    AMELTECH_DUPLICATE = -11,
    AMELTECH_CONFLICT = -12,
    AMELTECH_OVERFLOW = -13
};

// ---------------------------------------------------------------------------
// Confidence bands produced by the matcher
//   >= 0.90  strong   - answered directly
//   >= 0.74  moderate - answered directly
//   >= 0.52  weak     - answered with a hedge
//   <  0.52  unknown  - the bot says so and offers the closest topic
// ---------------------------------------------------------------------------

struct ContextEntry {
    char question[AMELTECH_CONTEXT_TEXT_LEN];
    char topic[AMELTECH_CONTEXT_TOPIC_LEN];
    char category[16];
    uint32_t timestamp;
    bool used;
};

class AmelTechBot {
public:
    AmelTechBot();
    ~AmelTechBot();

    // ---- lifecycle ------------------------------------------------------
    bool begin();
    void end();

    // Call from loop(). Refreshes sensors, thermal state and telemetry at
    // their own safe intervals and saves anything dirty. Cheap to call often.
    void tick();

    // ---- conversation ---------------------------------------------------
    String ask(const String& question);
    String ask(const char* question);

    // One entry point for a line typed into the Serial Monitor. It routes
    // training commands to the console and everything else to ask().
    String handleSerialLine(const String& line);

    // Convenience: reads a full line from a stream when one is ready.
    // Returns true and fills `out` with the reply when a line was handled.
    bool pollSerial(Stream& stream, String& out);

    // ---- knowledge ------------------------------------------------------
    AmelTechError train(const String& question, const String& answer,
                        const String& category = "custom");
    AmelTechError addQA(const String& question, const String& answer,
                        const String& category = "custom");
    AmelTechError removeQA(const String& question);
    AmelTechError removeQAByCode(uint16_t code);
    void clearKnowledge();                 // taught entries only
    size_t getKnowledgeCount() const;      // built-in + taught
    size_t getBuiltinCount() const;
    size_t getUserCount() const;
    uint16_t getLastTrainCode() const;

    AmelTechError saveKnowledge();
    AmelTechError loadKnowledge();

    // ---- calculator -----------------------------------------------------
    String calculate(const String& expression);
    String calculate(const char* expression);

    // ---- sensors --------------------------------------------------------
    bool beginDHT(uint8_t pin, DhtType type = DHT_TYPE_22);
    void endDHT();
    bool readSensors(bool force = false);
    const DhtReading& getSensorReading() const;
    String getSensorReport();
    String getSituationReport();
    bool hasSensor() const;

    // ---- identity -------------------------------------------------------
    const char* getUserName() const;
    const char* getUserField() const;
    bool rememberUser(const String& name, const String& field = "");
    bool forgetUser(const String& name);
    void forgetAllUsers();
    String listUsers() const;
    size_t getUserProfileCount() const;

    // ---- context --------------------------------------------------------
    void resetContext();
    void setContextSize(uint8_t size);
    uint8_t getContextSize() const;

    // ---- personality ----------------------------------------------------
    void enableTrolling(bool enable);
    bool isTrollingEnabled() const;
    void setName(const String& botName);
    const char* getName() const;

    // ---- status ---------------------------------------------------------
    AmelTechError getLastError() const;
    const char* getLastStatus() const;
    float getConfidence() const;
    MeasurementStatus getMeasurementStatus() const;
    uint32_t getLastScanMicros() const;

    // ---- hardware -------------------------------------------------------
    const ESP32Telemetry& getTelemetry(bool full = false);
    String runDiagnostics(bool full = false);
    String getHealthReport();
    int getHealthScore();
    String getThermalReport();

    // ---- subsystem access (advanced use) --------------------------------
    KnowledgeBase& knowledge() { return _kb; }
    Calculator& calculator() { return _calc; }
    SensorHub& sensors() { return _sensors; }
    Telemetry& telemetry() { return _telemetry; }
    ThermalGuard& thermal() { return _thermal; }
    Diagnostics& diagnostics() { return _diag; }
    UserProfileStore& profiles() { return _profiles; }
    IdentityManager& identity() { return _identity; }
    TrainingConsole& training() { return _console; }

    static const char* version() { return AMELTECH_VERSION_STRING; }
    static const char* errorToString(AmelTechError err);

private:
    KnowledgeBase _kb;
    Calculator _calc;
    SensorHub _sensors;
    Telemetry _telemetry;
    ThermalGuard _thermal;
    Diagnostics _diag;
    UserProfileStore _profiles;
    IdentityManager _identity;
    TrainingConsole _console;

    ContextEntry _context[AMELTECH_MAX_CONTEXT];
    uint8_t _contextSize;
    uint8_t _contextCount;

    bool _ready;
    bool _trolling;
    char _botName[24];
    AmelTechError _lastError;
    char _lastStatus[AMELTECH_STATUS_LEN];
    float _lastConfidence;
    MeasurementStatus _lastMeasurement;

    uint32_t _turnCount;
    uint32_t _lastTickMs;
    uint32_t _lastSaveMs;
    String _serialBuffer;

    // pipeline stages
    String answerQuestion(const String& raw, bool allowIdentity);
    String tryKnowledge(const String& raw, float& confidenceOut, bool& foundOut,
                        String& topicOut, String& categoryOut);
    static bool rewriteQuery(const String& raw, uint8_t attempt, String& out);

    String handleSmallTalk(const AmelTechQuery& q, bool& handled);
    String handleSensorQuery(const AmelTechQuery& q, bool& handled);
    String handleHardwareQuery(const AmelTechQuery& q, bool& handled);
    String handleFollowUp(const AmelTechQuery& q, bool& handled);
    String handleSelfQuery(const AmelTechQuery& q, bool& handled);

    String buildFallback(const String& raw);
    String decorateWithName(const String& reply);
    String maybeTroll(const String& answer, const char* category);

    void pushContext(const char* question, const char* topic, const char* category);
    const ContextEntry* lastContext() const;

    void setError(AmelTechError err, const char* status);
    void setStatus(const char* status);
    bool looksLikeMath(const String& raw) const;
};

#endif // AMELTECH_BOT_H
