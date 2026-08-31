#include "SensorHub.h"
#include "AmelTechLog.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if defined(ESP32)
static portMUX_TYPE amelDhtMux = portMUX_INITIALIZER_UNLOCKED;
#endif

// ---------------------------------------------------------------------------
SensorHub::SensorHub()
    : _pin(255),
      _type(DHT_NONE),
      _interruptSafe(true),
      _lastAttemptMs(0),
      _trollIndex(0),
      _historyCount(0),
      _historyHead(0) {
    memset(&_reading, 0, sizeof(_reading));
    _reading.status = MEAS_UNAVAILABLE;
    _reading.lastResult = DHT_NOT_CONFIGURED;
    _reading.temperatureC = NAN;
    _reading.humidityPercent = NAN;
    for (uint8_t i = 0; i < AMELTECH_DHT_HISTORY; ++i) {
        _tempHistory[i] = NAN;
        _humHistory[i] = NAN;
    }
}

const char* SensorHub::typeName() const {
    switch (_type) {
        case DHT_TYPE_11: return "DHT11";
        case DHT_TYPE_21: return "DHT21";
        case DHT_TYPE_22: return "DHT22";
        default: return "none";
    }
}

const char* SensorHub::resultName(DhtReadStatus s) {
    switch (s) {
        case DHT_OK:              return "OK";
        case DHT_NOT_CONFIGURED:  return "no sensor configured";
        case DHT_TOO_SOON:        return "rate limited";
        case DHT_NO_RESPONSE:     return "no response from sensor";
        case DHT_TIMEOUT:         return "timed out mid-transfer";
        case DHT_CHECKSUM:        return "checksum mismatch";
        case DHT_IMPLAUSIBLE:     return "values out of range";
        default:                  return "unknown";
    }
}

uint32_t SensorHub::minIntervalMs() const {
    return (_type == DHT_TYPE_11) ? AMELTECH_DHT11_MIN_INTERVAL_MS
                                  : AMELTECH_DHT22_MIN_INTERVAL_MS;
}

bool SensorHub::beginDht(uint8_t pin, DhtType type) {
    if (type != DHT_TYPE_11 && type != DHT_TYPE_21 && type != DHT_TYPE_22) {
        _type = DHT_NONE;
        _reading.lastResult = DHT_NOT_CONFIGURED;
        _reading.status = MEAS_UNSUPPORTED;
        return false;
    }
    _pin = pin;
    _type = type;
    _trollIndex = 0;
    _historyCount = 0;
    _historyHead = 0;
    memset(&_reading, 0, sizeof(_reading));
    _reading.temperatureC = NAN;
    _reading.humidityPercent = NAN;
    _reading.status = MEAS_UNAVAILABLE;
    _reading.lastResult = DHT_TIMEOUT;

    pinMode(_pin, INPUT_PULLUP);
    // The datasheet asks for a settling period after power-up.
    _lastAttemptMs = millis();

    AmelTechLogger.log(AMELTECH_LOG_INFO, "DHT %s on GPIO%u", typeName(), (unsigned)_pin);
    return true;
}

void SensorHub::endDht() {
    _type = DHT_NONE;
    _pin = 255;
    _reading.status = MEAS_UNAVAILABLE;
    _reading.lastResult = DHT_NOT_CONFIGURED;
}

uint32_t SensorHub::ageMs() const {
    if (_reading.timestampMs == 0) return 0xFFFFFFFFUL;
    uint32_t now = millis();
    return (now >= _reading.timestampMs) ? (now - _reading.timestampMs)
                                         : (0xFFFFFFFFUL - _reading.timestampMs + now);
}

bool SensorHub::isFresh(uint32_t maxAgeMs) const {
    if (_reading.status != MEAS_LIVE && _reading.status != MEAS_CACHED) return false;
    return ageMs() <= maxAgeMs;
}

// ---------------------------------------------------------------------------
// Single-wire protocol
// ---------------------------------------------------------------------------
uint32_t SensorHub::expectPulse(uint8_t pin, uint8_t level, uint32_t timeoutUs) {
    uint32_t start = micros();
    while (digitalRead(pin) == (int)level) {
        uint32_t elapsed = micros() - start;
        if (elapsed > timeoutUs) return 0;
    }
    uint32_t width = micros() - start;
    return width == 0 ? 1 : width;   // 0 is reserved for "timed out"
}

DhtReadStatus SensorHub::readRaw(uint8_t* data) {
    memset(data, 0, 5);

    // 1. Wake the sensor with a start pulse.
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    if (_type == DHT_TYPE_11) {
        delay(20);              // DHT11 wants at least 18 ms
    } else {
        delayMicroseconds(1200); // DHT22 wants at least 1 ms
    }

    // 2. Release the line and let the pull-up take over.
    pinMode(_pin, INPUT_PULLUP);
    delayMicroseconds(45);

    uint32_t cycles[80];

    {
#if defined(ESP32)
        bool guarded = _interruptSafe;
        if (guarded) portENTER_CRITICAL(&amelDhtMux);
#else
        bool guarded = _interruptSafe;
        if (guarded) noInterrupts();
#endif

        // 3. Handshake: 80 us low then 80 us high from the sensor.
        if (expectPulse(_pin, HIGH, 120) == 0) {
#if defined(ESP32)
            if (guarded) portEXIT_CRITICAL(&amelDhtMux);
#else
            if (guarded) interrupts();
#endif
            return DHT_NO_RESPONSE;
        }
        if (expectPulse(_pin, LOW, 140) == 0) {
#if defined(ESP32)
            if (guarded) portEXIT_CRITICAL(&amelDhtMux);
#else
            if (guarded) interrupts();
#endif
            return DHT_NO_RESPONSE;
        }
        if (expectPulse(_pin, HIGH, 140) == 0) {
#if defined(ESP32)
            if (guarded) portEXIT_CRITICAL(&amelDhtMux);
#else
            if (guarded) interrupts();
#endif
            return DHT_NO_RESPONSE;
        }

        // 4. 40 data bits, each a ~50 us low followed by a 26 us (0) or
        //    70 us (1) high. Widths are captured first and decoded after, so
        //    the timing-critical loop stays as short as possible.
        for (int i = 0; i < 80; i += 2) {
            cycles[i]     = expectPulse(_pin, LOW, 90);
            cycles[i + 1] = expectPulse(_pin, HIGH, 110);
        }

#if defined(ESP32)
        if (guarded) portEXIT_CRITICAL(&amelDhtMux);
#else
        if (guarded) interrupts();
#endif
    }

    // 5. Decode by comparing each high pulse against its own preceding low
    //    pulse, which makes the decision independent of clock accuracy.
    for (int i = 0; i < 40; ++i) {
        uint32_t lowCycles = cycles[2 * i];
        uint32_t highCycles = cycles[2 * i + 1];
        if (lowCycles == 0 || highCycles == 0) return DHT_TIMEOUT;
        data[i / 8] <<= 1;
        if (highCycles > lowCycles) data[i / 8] |= 1;
    }

    uint8_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    if (sum != data[4]) return DHT_CHECKSUM;
    return DHT_OK;
}

DhtReadStatus SensorHub::read(bool force) {
    if (_type == DHT_NONE) {
        _reading.lastResult = DHT_NOT_CONFIGURED;
        _reading.status = MEAS_UNSUPPORTED;
        return DHT_NOT_CONFIGURED;
    }

    uint32_t now = millis();
    uint32_t since = now - _lastAttemptMs;
    if (!force && since < minIntervalMs()) {
        // Respect the datasheet. The cached value is still meaningful.
        if (_reading.status == MEAS_LIVE) _reading.status = MEAS_CACHED;
        _reading.lastResult = DHT_TOO_SOON;
        return DHT_TOO_SOON;
    }
    _lastAttemptMs = now;

    uint8_t data[5];
    DhtReadStatus rc = DHT_TIMEOUT;
    for (uint8_t attempt = 0; attempt < AMELTECH_DHT_MAX_RETRIES; ++attempt) {
        rc = readRaw(data);
        if (rc == DHT_OK) break;
        delay(5);
    }

    if (rc != DHT_OK) {
        _reading.lastResult = rc;
        ++_reading.failureCount;
        ++_reading.consecutiveFailures;
        // A previously good value becomes stale, it does not become a lie.
        _reading.status = (_reading.successCount > 0) ? MEAS_STALE : MEAS_ERROR;
        AmelTechLogger.log(AMELTECH_LOG_WARN, "DHT read failed: %s", resultName(rc));
        return rc;
    }

    float h, t;
    if (_type == DHT_TYPE_11) {
        h = (float)data[0] + (float)(data[1] & 0x0F) * 0.1f;
        t = (float)(data[2] & 0x7F) + (float)(data[3] & 0x0F) * 0.1f;
        if (data[2] & 0x80) t = -t;
    } else {
        h = (float)(((uint16_t)data[0] << 8) | data[1]) * 0.1f;
        uint16_t raw = ((uint16_t)(data[2] & 0x7F) << 8) | data[3];
        t = (float)raw * 0.1f;
        if (data[2] & 0x80) t = -t;
    }

    // Range check against the datasheets before trusting the numbers.
    bool plausible = (h >= 0.0f && h <= 100.0f);
    if (_type == DHT_TYPE_11) plausible = plausible && (t >= -20.0f && t <= 60.0f);
    else plausible = plausible && (t >= -40.0f && t <= 80.0f);

    if (!plausible) {
        _reading.lastResult = DHT_IMPLAUSIBLE;
        ++_reading.failureCount;
        ++_reading.consecutiveFailures;
        _reading.status = (_reading.successCount > 0) ? MEAS_STALE : MEAS_ERROR;
        AmelTechLogger.log(AMELTECH_LOG_WARN, "DHT implausible t=%d h=%d", (int)t, (int)h);
        return DHT_IMPLAUSIBLE;
    }

    _reading.temperatureC = t;
    _reading.humidityPercent = h;
    _reading.status = MEAS_LIVE;
    _reading.lastResult = DHT_OK;
    _reading.timestampMs = now;
    ++_reading.successCount;
    _reading.consecutiveFailures = 0;
    pushHistory(t, h);
    return DHT_OK;
}

// ---------------------------------------------------------------------------
// Derived physics
// ---------------------------------------------------------------------------
float SensorHub::dewPoint(float tempC, float humidity) {
    if (humidity <= 0.0f || humidity > 100.0f) return NAN;
    const float a = 17.62f;
    const float b = 243.12f;
    float gamma = logf(humidity / 100.0f) + (a * tempC) / (b + tempC);
    return (b * gamma) / (a - gamma);
}

float SensorHub::heatIndex(float tempC, float humidity) {
    // Rothfusz regression, defined in Fahrenheit and only meaningful in warm,
    // humid conditions. Below the valid range the apparent temperature is
    // simply the air temperature.
    float tF = tempC * 9.0f / 5.0f + 32.0f;
    if (tF < 80.0f || humidity < 40.0f) return tempC;

    float hi = -42.379f
             + 2.04901523f * tF
             + 10.14333127f * humidity
             - 0.22475541f * tF * humidity
             - 0.00683783f * tF * tF
             - 0.05481717f * humidity * humidity
             + 0.00122874f * tF * tF * humidity
             + 0.00085282f * tF * humidity * humidity
             - 0.00000199f * tF * tF * humidity * humidity;
    return (hi - 32.0f) * 5.0f / 9.0f;
}

float SensorHub::absoluteHumidity(float tempC, float humidity) {
    // Grams of water vapour per cubic metre of air.
    float saturation = 6.112f * expf((17.67f * tempC) / (tempC + 243.5f));
    return (saturation * humidity * 2.1674f) / (273.15f + tempC);
}

ComfortLevel SensorHub::classifyComfort(float tempC, float humidity) {
    float apparent = heatIndex(tempC, humidity);
    if (apparent >= 41.0f) return COMFORT_DANGEROUS;
    if (apparent >= 33.0f) return COMFORT_HOT;
    if (apparent >= 27.0f) return COMFORT_WARM;
    if (tempC >= 20.0f) {
        if (humidity > 70.0f) return COMFORT_WARM;
        if (humidity < 25.0f) return COMFORT_COOL;
        return COMFORT_IDEAL;
    }
    if (tempC >= 16.0f) return COMFORT_COOL;
    return COMFORT_COLD;
}

const char* SensorHub::comfortName(ComfortLevel c) {
    switch (c) {
        case COMFORT_COLD:      return "cold";
        case COMFORT_COOL:      return "cool";
        case COMFORT_IDEAL:     return "comfortable";
        case COMFORT_WARM:      return "warm";
        case COMFORT_HOT:       return "hot";
        case COMFORT_DANGEROUS: return "dangerously hot";
        default:                return "unknown";
    }
}

const char* SensorHub::trendName(TrendDirection t) {
    switch (t) {
        case TREND_STEADY:  return "steady";
        case TREND_RISING:  return "rising";
        case TREND_FALLING: return "falling";
        default:            return "unknown";
    }
}

void SensorHub::pushHistory(float t, float h) {
    _tempHistory[_historyHead] = t;
    _humHistory[_historyHead] = h;
    _historyHead = (uint8_t)((_historyHead + 1) % AMELTECH_DHT_HISTORY);
    if (_historyCount < AMELTECH_DHT_HISTORY) ++_historyCount;
}

TrendDirection SensorHub::trendOf(const float* history) const {
    if (_historyCount < 4) return TREND_UNKNOWN;

    // Compare the mean of the older half against the mean of the newer half.
    uint8_t n = _historyCount;
    uint8_t half = (uint8_t)(n / 2);
    float oldSum = 0.0f, newSum = 0.0f;
    uint8_t oldN = 0, newN = 0;

    for (uint8_t i = 0; i < n; ++i) {
        uint8_t pos = (uint8_t)((_historyHead + AMELTECH_DHT_HISTORY - n + i) % AMELTECH_DHT_HISTORY);
        float v = history[pos];
        if (isnan(v)) continue;
        if (i < half) { oldSum += v; ++oldN; }
        else { newSum += v; ++newN; }
    }
    if (oldN == 0 || newN == 0) return TREND_UNKNOWN;

    float delta = (newSum / newN) - (oldSum / oldN);
    if (delta > 0.5f) return TREND_RISING;
    if (delta < -0.5f) return TREND_FALLING;
    return TREND_STEADY;
}

// ---------------------------------------------------------------------------
SituationReport SensorHub::analyze() {
    SituationReport r;
    memset(&r, 0, sizeof(r));
    r.valid = false;
    r.comfort = COMFORT_UNKNOWN;
    r.temperatureTrend = TREND_UNKNOWN;
    r.humidityTrend = TREND_UNKNOWN;
    r.temperatureC = NAN;
    r.humidityPercent = NAN;
    r.dewPointC = NAN;
    r.heatIndexC = NAN;
    r.absoluteHumidity = NAN;

    if (_type == DHT_NONE) {
        snprintf(r.headline, sizeof(r.headline),
                 "No DHT sensor is configured, so I have nothing to measure.");
        snprintf(r.advice, sizeof(r.advice),
                 "Call beginDHT(pin, AMELTECH_DHT22) or AMELTECH_DHT11 in setup().");
        return r;
    }
    if (_reading.status != MEAS_LIVE && _reading.status != MEAS_CACHED) {
        snprintf(r.headline, sizeof(r.headline),
                 "%s reading is %s (%s).", typeName(),
                 Telemetry::statusToString(_reading.status),
                 resultName(_reading.lastResult));
        snprintf(r.advice, sizeof(r.advice),
                 "Check wiring on GPIO%u, the 3V3 supply and the pull-up resistor.",
                 (unsigned)_pin);
        return r;
    }

    float t = _reading.temperatureC;
    float h = _reading.humidityPercent;

    r.valid = true;
    r.temperatureC = t;
    r.humidityPercent = h;
    r.dewPointC = dewPoint(t, h);
    r.heatIndexC = heatIndex(t, h);
    r.absoluteHumidity = absoluteHumidity(t, h);
    r.comfort = classifyComfort(t, h);
    r.temperatureTrend = trendOf(_tempHistory);
    r.humidityTrend = trendOf(_humHistory);
    r.condensationRisk = (!isnan(r.dewPointC) && (t - r.dewPointC) < 2.0f);
    r.moldRisk = (h > 70.0f);
    r.dryAirRisk = (h < 25.0f);
    r.electronicsRisk = (t > 50.0f) || (h > 85.0f) || r.condensationRisk;

    snprintf(r.headline, sizeof(r.headline),
             "%.1f C and %.0f%% RH: %s. Feels like %.1f C, dew point %.1f C.",
             (double)t, (double)h, comfortName(r.comfort),
             (double)r.heatIndexC, (double)r.dewPointC);

    if (r.comfort == COMFORT_DANGEROUS) {
        snprintf(r.advice, sizeof(r.advice),
                 "Heat stress range. Ventilate or cool the area, and keep the board out of direct sun.");
    } else if (r.condensationRisk) {
        snprintf(r.advice, sizeof(r.advice),
                 "Air is within %.1f C of the dew point, so moisture can condense on cold surfaces and PCBs.",
                 (double)(t - r.dewPointC));
    } else if (r.moldRisk) {
        snprintf(r.advice, sizeof(r.advice),
                 "Humidity above 70%% for a while encourages mould and corrodes headers. Improve airflow.");
    } else if (r.dryAirRisk) {
        snprintf(r.advice, sizeof(r.advice),
                 "Below 25%% RH raises static discharge risk. Ground yourself before handling boards.");
    } else if (r.comfort == COMFORT_COLD) {
        snprintf(r.advice, sizeof(r.advice),
                 "Cold but harmless for the board. DHT11 accuracy degrades below 0 C.");
    } else {
        snprintf(r.advice, sizeof(r.advice),
                 "Conditions are fine for both people and electronics. Nothing needs attention.");
    }
    return r;
}

// ---------------------------------------------------------------------------
// Situational humour
//
// Lines are chosen from the band the measurement actually falls in, then
// rotated so the same conditions do not always produce the same joke. The
// numbers always come first; the joke is only ever an addition.
// ---------------------------------------------------------------------------
static const char* const TROLL_DANGEROUS[] = {
    "I am a microcontroller, not a rotisserie. Please open a window.",
    "At this point the room is cooking, and I am the main course.",
    "My thermal budget just filed a complaint with management."
};
static const char* const TROLL_HOT[] = {
    "Warm enough that my solder is thinking about a career change.",
    "If it gets any hotter I am going to start claiming overtime.",
    "This is fine. Everything is fine. I am definitely not sweating."
};
static const char* const TROLL_WARM[] = {
    "Pleasant, if you are a lizard.",
    "Toasty. My capacitors are pretending to enjoy it.",
    "Warm, but not yet worth panicking about."
};
static const char* const TROLL_IDEAL[] = {
    "Frankly perfect. Nothing to complain about, which is disappointing.",
    "Ideal conditions. I have nothing dramatic to report and I hate it.",
    "Textbook comfortable. Someone hand the room an award."
};
static const char* const TROLL_COOL[] = {
    "A bit chilly. My crystal oscillator is shivering, poetically speaking.",
    "Cool enough that I would put on a jumper, if I owned one.",
    "Brisk. Great for the CPU, mildly rude to the humans."
};
static const char* const TROLL_COLD[] = {
    "Cold. I am a chip, not a penguin.",
    "At these temperatures even the electrons are moving reluctantly.",
    "Freezing. The DHT11 datasheet and I both want to go home."
};
static const char* const TROLL_HUMID[] = {
    "That humidity is less 'weather' and more 'soup'.",
    "Damp enough that my PCB is considering waterproofing.",
    "The air is basically drinkable. I do not recommend drinking it."
};
static const char* const TROLL_DRY[] = {
    "So dry that static electricity is now a personality trait.",
    "This air could dehydrate a cactus.",
    "Bone dry. Touch the board and you will both regret it."
};

String SensorHub::troll(const SituationReport& s) {
    if (!s.valid) {
        return String(F("I would make a joke about the weather, but I genuinely cannot measure it right now."));
    }

    const char* const* pool;
    uint8_t poolSize = 3;

    if (s.comfort == COMFORT_DANGEROUS) pool = TROLL_DANGEROUS;
    else if (s.comfort == COMFORT_HOT) pool = TROLL_HOT;
    else if (s.humidityPercent > 80.0f) pool = TROLL_HUMID;
    else if (s.humidityPercent < 20.0f) pool = TROLL_DRY;
    else if (s.comfort == COMFORT_WARM) pool = TROLL_WARM;
    else if (s.comfort == COMFORT_IDEAL) pool = TROLL_IDEAL;
    else if (s.comfort == COMFORT_COOL) pool = TROLL_COOL;
    else pool = TROLL_COLD;

    const char* line = pool[_trollIndex % poolSize];
    _trollIndex = (uint8_t)(_trollIndex + 1);
    return String(line);
}
