#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace ameltech {

enum class Status : uint8_t {
    OK = 0,
    INVALID_INPUT,
    NOT_FOUND,
    LOW_CONFIDENCE,
    UNSUPPORTED,
    UNAVAILABLE,
    MEASUREMENT_ERROR,
    MEMORY_ERROR,
    STORAGE_ERROR,
    TIMEOUT,
    INVALID_CONFIGURATION
};

const char* statusName(Status status);

enum class TelemetryState : uint8_t {
    LIVE = 0,
    CACHED,
    STALE,
    UNAVAILABLE,
    UNSUPPORTED,
    MEASUREMENT_ERROR
};

const char* telemetryStateName(TelemetryState state);

enum class HealthLevel : uint8_t { NORMAL = 0, INFO, WARNING, HIGH, CRITICAL, UNKNOWN };
const char* healthLevelName(HealthLevel level);

enum class Intent : uint8_t {
    UNKNOWN = 0, GENERAL, SCIENCE, ESP32, CPU, MEMORY, WIFI, BLUETOOTH, UART,
    I2C, SPI, GPIO, ADC, DAC, PWM, SYSTEM, CALCULATOR, TRAINING, DIAGNOSTICS,
    PERFORMANCE, STORAGE, TEMPERATURE
};

struct Confidence {
    float answer = 0.0f;
    float measurement = 0.0f;
    float dataFreshness = 0.0f;
    float knowledgeQuality = 0.0f;
};

struct MeasureFloat {
    float value = 0.0f;
    TelemetryState state = TelemetryState::UNAVAILABLE;
    uint32_t ageMs = 0;
    float confidence = 0.0f;
};

struct MeasureUInt32 {
    uint32_t value = 0;
    TelemetryState state = TelemetryState::UNAVAILABLE;
    uint32_t ageMs = 0;
    float confidence = 0.0f;
};

struct MeasureBool {
    bool value = false;
    TelemetryState state = TelemetryState::UNAVAILABLE;
    uint32_t ageMs = 0;
    float confidence = 0.0f;
};

struct MeasureText {
    char value[48] = {0};
    TelemetryState state = TelemetryState::UNAVAILABLE;
    uint32_t ageMs = 0;
    float confidence = 0.0f;
};

struct ESP32Telemetry {
    MeasureText chip;
    MeasureUInt32 cpuMHz;
    MeasureUInt32 freeHeap;
    MeasureUInt32 minFreeHeap;
    MeasureUInt32 uptimeMs;
    MeasureUInt32 flashSize;
    MeasureUInt32 sketchSize;
    MeasureUInt32 freeSketchSpace;
    MeasureFloat wifiRSSI;
    MeasureBool wifiConnected;
    MeasureFloat wifiLatencyMs;
    MeasureFloat temperatureC;
    MeasureUInt32 errorCount;
    MeasureText resetReason;
    MeasureText architecture;
    MeasureText capabilities;
    MeasureUInt32 gpioCount;
    MeasureUInt32 nvsUsed;
    MeasureUInt32 nvsTotal;
};

struct DiagnosticResult {
    Status status = Status::UNAVAILABLE;
    HealthLevel level = HealthLevel::UNKNOWN;
    uint8_t score = 0;
    char summary[160] = {0};
    char details[512] = {0};
};

struct Response {
    Status status = Status::UNAVAILABLE;
    Intent intent = Intent::UNKNOWN;
    Confidence confidence;
    bool needsClarification = false;
    char text[512] = {0};
};

struct QAEntry {
    char question[128];
    char answer[256];
    char category[32];
};

class KnowledgeBase {
public:
    static constexpr size_t MaxEntries = 48;
    Status addQA(const char* question, const char* answer, const char* category = "general");
    Status train(const char* question, const char* answer, const char* category = "general") { return addQA(question, answer, category); }
    Status removeQA(const char* question);
    void clear();
    size_t count() const { return count_; }
    const QAEntry* at(size_t index) const;
    Status saveKnowledge(const char* namespaceName = "amelbot");
    Status loadKnowledge(const char* namespaceName = "amelbot");
    Status lastStatus() const { return lastStatus_; }

private:
    QAEntry entries_[MaxEntries]{};
    size_t count_ = 0;
    Status lastStatus_ = Status::OK;
};

class Calculator {
public:
    bool evaluate(const char* expression, double& result) const;
    Status evaluateText(const char* expression, char* out, size_t outSize) const;
};

class TelemetryEngine {
public:
    explicit TelemetryEngine(ESP32Telemetry& telemetry);
    Status refreshFast();
    Status refreshSlow();
    Status fullScan();
    const ESP32Telemetry& get() const { return telemetry_; }
    void setErrorCount(uint32_t count) { telemetry_.errorCount.value = count; telemetry_.errorCount.state = TelemetryState::LIVE; }
    uint32_t benchmarkLoop(uint16_t samples = 16);
    uint32_t measureElapsedMicros(uint32_t (*fn)(void*), void* context, uint16_t repeats = 8) const;

private:
    ESP32Telemetry& telemetry_;
    uint32_t fastUpdatedAt_ = 0;
    uint32_t slowUpdatedAt_ = 0;
    void markUnavailable();
};

class HealthEngine {
public:
    DiagnosticResult evaluate(const ESP32Telemetry& t) const;
};

class AmelTechBot {
public:
    explicit AmelTechBot(Stream* output = nullptr);
    Status begin();
    Response ask(const char* question);
    Status train(const char* question, const char* answer, const char* category = "general") { return kb_.train(question, answer, category); }
    Status addQA(const char* question, const char* answer, const char* category = "general") { return kb_.addQA(question, answer, category); }
    Status removeQA(const char* question) { return kb_.removeQA(question); }
    void clearKnowledge() { kb_.clear(); }
    size_t getKnowledgeCount() const { return kb_.count(); }
    Status saveKnowledge(const char* namespaceName = "amelbot") { return kb_.saveKnowledge(namespaceName); }
    Status loadKnowledge(const char* namespaceName = "amelbot") { return kb_.loadKnowledge(namespaceName); }
    Status lastError() const { return lastError_; }
    ESP32Telemetry& telemetry() { return telemetry_; }
    const ESP32Telemetry& telemetry() const { return telemetry_; }
    TelemetryEngine& diagnostics() { return telemetryEngine_; }
    const DiagnosticResult& health() const { return health_; }
    void enableTrolling(bool enabled) { trolling_ = enabled; }
    bool trollingEnabled() const { return trolling_; }
    void setContextCapacity(uint8_t capacity) { contextCapacity_ = capacity > 8 ? 8 : capacity; }

private:
    KnowledgeBase kb_;
    Calculator calculator_;
    ESP32Telemetry telemetry_{};
    TelemetryEngine telemetryEngine_;
    HealthEngine healthEngine_;
    DiagnosticResult health_{};
    Stream* output_;
    bool trolling_ = false;
    uint8_t contextCapacity_ = 4;
    char previousQuestion_[128] = {0};
    char previousAnswer_[512] = {0};
    Status lastError_ = Status::OK;

    Intent detectIntent(const char* normalized) const;
    Response answerKnowledge(const char* question, Intent intent);
    Response answerTelemetry(const char* question, Intent intent);
    Response answerCalculator(const char* question);
    Response clarify(Intent intent) const;
    void updateContext(const char* question, const char* answer);
    static void trimCopy(const char* in, char* out, size_t outSize);
};

} // namespace ameltech

using AmelTechBot = ameltech::AmelTechBot;
using ESP32Telemetry = ameltech::ESP32Telemetry;
