/*
 * SensorHub.h
 * ---------------------------------------------------------------------------
 * Self-contained DHT11 / DHT21 / DHT22 support plus the situation analysis the
 * chatbot answers from.
 *
 * There is no dependency on an external DHT library: the single-wire protocol
 * is implemented here so the library builds and behaves identically on any
 * ESP32 core.
 *
 * Honesty rules that the rest of the library relies on:
 *   - Every reading carries a MeasurementStatus. A failed read is reported as
 *     an error or as stale, never silently replaced by the last good value.
 *   - The sensor is never polled faster than its datasheet allows.
 *   - Derived values (dew point, heat index, absolute humidity) are computed
 *     from the live reading and are marked unavailable when it is.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_SENSOR_HUB_H
#define AMELTECH_SENSOR_HUB_H

#include <Arduino.h>
#include "AmelTechConfig.h"
#include "Telemetry.h"

enum DhtType : uint8_t {
    DHT_NONE = 0,
    DHT_TYPE_11 = 11,
    DHT_TYPE_21 = 21,
    DHT_TYPE_22 = 22
};

enum DhtReadStatus : uint8_t {
    DHT_OK = 0,
    DHT_NOT_CONFIGURED,
    DHT_TOO_SOON,          // rate limited; last good value still valid
    DHT_NO_RESPONSE,       // sensor never pulled the line down
    DHT_TIMEOUT,           // a pulse never arrived
    DHT_CHECKSUM,          // 40 bits received but the checksum failed
    DHT_IMPLAUSIBLE        // decoded values are outside the sensor's range
};

enum ComfortLevel : uint8_t {
    COMFORT_UNKNOWN = 0,
    COMFORT_COLD,
    COMFORT_COOL,
    COMFORT_IDEAL,
    COMFORT_WARM,
    COMFORT_HOT,
    COMFORT_DANGEROUS
};

enum TrendDirection : uint8_t {
    TREND_UNKNOWN = 0,
    TREND_STEADY,
    TREND_RISING,
    TREND_FALLING
};

struct DhtReading {
    float temperatureC;
    float humidityPercent;
    MeasurementStatus status;
    DhtReadStatus lastResult;
    uint32_t timestampMs;
    uint16_t successCount;
    uint16_t failureCount;
    uint16_t consecutiveFailures;
};

struct SituationReport {
    bool valid;
    float temperatureC;
    float humidityPercent;
    float dewPointC;
    float heatIndexC;
    float absoluteHumidity;    // g/m^3
    ComfortLevel comfort;
    TrendDirection temperatureTrend;
    TrendDirection humidityTrend;
    bool condensationRisk;     // ambient is within 2 C of the dew point
    bool moldRisk;             // sustained high humidity
    bool dryAirRisk;           // sustained low humidity
    bool electronicsRisk;      // conditions the board itself will not enjoy
    char headline[96];
    char advice[128];
};

class SensorHub {
public:
    SensorHub();

    // pin: any GPIO usable as input/output. A 4.7k-10k pull-up to 3V3 is
    // required; most breakout boards already include one.
    bool beginDht(uint8_t pin, DhtType type);
    void endDht();

    bool isConfigured() const { return _type != DHT_NONE; }
    DhtType type() const { return _type; }
    uint8_t pin() const { return _pin; }
    const char* typeName() const;

    // Reads at most once per datasheet interval; otherwise returns the cached
    // value and reports DHT_TOO_SOON.
    DhtReadStatus read(bool force = false);

    const DhtReading& reading() const { return _reading; }

    // How long ago the last good reading was taken, in milliseconds.
    uint32_t ageMs() const;
    bool isFresh(uint32_t maxAgeMs = 30000) const;

    SituationReport analyze();

    // Physical helpers, usable independently.
    static float dewPoint(float tempC, float humidity);
    static float heatIndex(float tempC, float humidity);
    static float absoluteHumidity(float tempC, float humidity);
    static ComfortLevel classifyComfort(float tempC, float humidity);
    static const char* comfortName(ComfortLevel c);
    static const char* trendName(TrendDirection t);
    static const char* resultName(DhtReadStatus s);

    // Situational humour. Always returned alongside real numbers, never
    // instead of them, and never for a reading the library does not have.
    String troll(const SituationReport& s);
    void resetTrollRotation() { _trollIndex = 0; }

    // Interrupts are disabled only for the ~4 ms bit-banged read. Turning this
    // off trades reliability for never blocking other interrupt work.
    void setInterruptSafe(bool enable) { _interruptSafe = enable; }
    bool interruptSafe() const { return _interruptSafe; }

private:
    uint8_t _pin;
    DhtType _type;
    bool _interruptSafe;
    DhtReading _reading;
    uint32_t _lastAttemptMs;
    uint8_t _trollIndex;

    float _tempHistory[AMELTECH_DHT_HISTORY];
    float _humHistory[AMELTECH_DHT_HISTORY];
    uint8_t _historyCount;
    uint8_t _historyHead;

    uint32_t minIntervalMs() const;
    DhtReadStatus readRaw(uint8_t* data);
    static uint32_t expectPulse(uint8_t pin, uint8_t level, uint32_t timeoutUs);
    void pushHistory(float t, float h);
    TrendDirection trendOf(const float* history) const;
};

#endif // AMELTECH_SENSOR_HUB_H
