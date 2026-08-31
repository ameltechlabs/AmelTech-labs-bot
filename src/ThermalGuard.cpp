#include "ThermalGuard.h"
#include "AmelTechLog.h"
#include <math.h>

ThermalGuard::ThermalGuard()
    : _telemetry(nullptr),
      _enabled(true),
      _state(THERMAL_UNKNOWN),
      _source(THERMAL_SRC_NONE),
      _temperature(NAN),
      _throttled(false),
      _throttleEvents(0),
      _originalCpuMhz(0),
      _sliceStartMs(0),
      _lastSliceMs(0),
      _busyMsAccum(0),
      _windowStartMs(0),
      _dutyPercent(0) {}

void ThermalGuard::begin(Telemetry* telemetry) {
    _telemetry = telemetry;
    _windowStartMs = millis();
    _busyMsAccum = 0;
#if defined(ESP32)
    _originalCpuMhz = getCpuFrequencyMhz();
#else
    _originalCpuMhz = 240;
#endif
}

const char* ThermalGuard::stateName(ThermalState s) {
    switch (s) {
        case THERMAL_NORMAL:   return "NORMAL";
        case THERMAL_WARM:     return "WARM";
        case THERMAL_HOT:      return "HOT";
        case THERMAL_CRITICAL: return "CRITICAL";
        default:               return "UNKNOWN";
    }
}

const char* ThermalGuard::sourceName(ThermalSource s) {
    switch (s) {
        case THERMAL_SRC_DIE:     return "die sensor";
        case THERMAL_SRC_AMBIENT: return "ambient DHT";
        default:                  return "none";
    }
}

void ThermalGuard::setEnabled(bool enable) {
    _enabled = enable;
    if (!enable && _throttled) {
#if defined(ESP32)
        if (_originalCpuMhz) setCpuFrequencyMhz(_originalCpuMhz);
#endif
        _throttled = false;
    }
}

void ThermalGuard::update() {
    if (!_telemetry) {
        _state = THERMAL_UNKNOWN;
        _source = THERMAL_SRC_NONE;
        return;
    }

    const ESP32Telemetry& t = _telemetry->data();

    // Prefer the die temperature; fall back to an attached ambient sensor.
    if (Telemetry::statusIsUsable(t.temperatureC.status)) {
        _temperature = t.temperatureC.value;
        _source = THERMAL_SRC_DIE;
    } else if (Telemetry::statusIsUsable(t.ambientTemperatureC.status)) {
        // Ambient runs cooler than the die. Treating ambient as if it were die
        // temperature would under-report, so a conservative offset is applied
        // and the source is reported honestly.
        _temperature = t.ambientTemperatureC.value + 15.0f;
        _source = THERMAL_SRC_AMBIENT;
    } else {
        _temperature = NAN;
        _source = THERMAL_SRC_NONE;
        _state = THERMAL_UNKNOWN;
        applyPolicy();
        return;
    }

    // Hysteresis: it takes a clearly lower reading to step back down a level,
    // so the clock cannot oscillate around a threshold.
    ThermalState next = _state;
    float hyst = AMELTECH_THERMAL_HYSTERESIS_C;

    if (_temperature >= AMELTECH_THERMAL_CRITICAL_C) {
        next = THERMAL_CRITICAL;
    } else if (_temperature >= AMELTECH_THERMAL_HIGH_C) {
        next = (_state == THERMAL_CRITICAL &&
                _temperature > AMELTECH_THERMAL_CRITICAL_C - hyst)
                   ? THERMAL_CRITICAL : THERMAL_HOT;
    } else if (_temperature >= AMELTECH_THERMAL_WARN_C) {
        next = (_state >= THERMAL_HOT && _temperature > AMELTECH_THERMAL_HIGH_C - hyst)
                   ? THERMAL_HOT : THERMAL_WARM;
    } else {
        next = (_state >= THERMAL_WARM && _temperature > AMELTECH_THERMAL_WARN_C - hyst)
                   ? THERMAL_WARM : THERMAL_NORMAL;
    }

    if (next != _state) {
        AmelTechLogger.log(next >= THERMAL_HOT ? AMELTECH_LOG_WARN : AMELTECH_LOG_INFO,
                           "thermal %s -> %s (%d C)",
                           stateName(_state), stateName(next), (int)_temperature);
        _state = next;
    }
    applyPolicy();
}

void ThermalGuard::applyPolicy() {
    if (!_enabled) return;
#if defined(ESP32)
    bool wantThrottle = (_state == THERMAL_HOT || _state == THERMAL_CRITICAL);
    if (wantThrottle && !_throttled) {
        if (_originalCpuMhz == 0) _originalCpuMhz = getCpuFrequencyMhz();
        if (setCpuFrequencyMhz(AMELTECH_THROTTLE_CPU_MHZ)) {
            _throttled = true;
            ++_throttleEvents;
            AmelTechLogger.log(AMELTECH_LOG_WARN, "throttled to %d MHz",
                               (int)AMELTECH_THROTTLE_CPU_MHZ);
        }
    } else if (!wantThrottle && _throttled) {
        if (_originalCpuMhz && setCpuFrequencyMhz(_originalCpuMhz)) {
            _throttled = false;
            AmelTechLogger.log(AMELTECH_LOG_INFO, "clock restored to %u MHz",
                               (unsigned)_originalCpuMhz);
        }
    }
#endif
}

void ThermalGuard::beginSlice() {
    _sliceStartMs = millis();
}

void ThermalGuard::tick() {
    uint32_t now = millis();
    uint32_t elapsed = now - _sliceStartMs;
    if (elapsed >= AMELTECH_MAX_COMPUTE_SLICE_MS) {
        _lastSliceMs = elapsed;
        _busyMsAccum += elapsed;

        // Give the scheduler and the watchdog a turn. When the part is hot the
        // pause is longer, which is the cheapest way to shed heat.
        AMELTECH_YIELD();
        if (_state == THERMAL_CRITICAL) delay(4);
        else if (_state == THERMAL_HOT) delay(2);

        _sliceStartMs = millis();
    }

    // Roll the duty window once a second.
    if (now - _windowStartMs >= 1000) {
        uint32_t window = now - _windowStartMs;
        if (window > 0) {
            uint32_t pct = (_busyMsAccum * 100) / window;
            _dutyPercent = (uint8_t)(pct > 100 ? 100 : pct);
        }
        _busyMsAccum = 0;
        _windowStartMs = now;
    }
}

uint8_t ThermalGuard::dutyPercent() const {
    return _dutyPercent;
}

String ThermalGuard::report() const {
    String out;
    out.reserve(160);
    out += F("Thermal: ");
    out += stateName(_state);
    if (_source == THERMAL_SRC_NONE) {
        out += F(" (no temperature source on this chip; workload limiting still active)");
        return out;
    }
    out += F(" at ");
    out += String(_temperature, 1);
    out += F(" C via ");
    out += sourceName(_source);
    if (_source == THERMAL_SRC_AMBIENT) {
        out += F(" (estimated die temperature)");
    }
    if (_throttled) {
        out += F(", CPU throttled to ");
        out += (int)AMELTECH_THROTTLE_CPU_MHZ;
        out += F(" MHz");
    }
    out += F(", duty ");
    out += (int)_dutyPercent;
    out += '%';
    return out;
}
