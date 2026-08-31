/*
 * ThermalGuard.h
 * ---------------------------------------------------------------------------
 * Keeps the module from cooking itself.
 *
 * Three levers, in order of how much they actually matter on an ESP32:
 *
 *   1. Do less work. The matcher yields cooperatively and the telemetry layer
 *      rate limits its reads, so the CPU is not spinning at 240 MHz answering
 *      the same question repeatedly.
 *   2. Slow down when hot. If the die temperature is readable and climbing,
 *      the clock is dropped to 80 MHz, which cuts core power substantially.
 *      Hysteresis prevents the clock from oscillating around the threshold.
 *   3. Say something. A thermal state is exposed so diagnostics and the chat
 *      layer can report it instead of silently degrading.
 *
 * On the original ESP32, which has no usable die temperature sensor, levers 1
 * and 3 still apply and an attached DHT is used as an ambient fallback. The
 * guard never claims a temperature it cannot measure.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_THERMAL_GUARD_H
#define AMELTECH_THERMAL_GUARD_H

#include <Arduino.h>
#include "AmelTechConfig.h"
#include "Telemetry.h"

enum ThermalState : uint8_t {
    THERMAL_UNKNOWN = 0,   // no temperature source at all
    THERMAL_NORMAL,
    THERMAL_WARM,          // watch it
    THERMAL_HOT,           // throttling applied
    THERMAL_CRITICAL       // reduce work aggressively
};

enum ThermalSource : uint8_t {
    THERMAL_SRC_NONE = 0,
    THERMAL_SRC_DIE,
    THERMAL_SRC_AMBIENT
};

class ThermalGuard {
public:
    ThermalGuard();

    void begin(Telemetry* telemetry);

    // Re-evaluates the thermal state and applies the clock policy.
    void update();

    ThermalState state() const { return _state; }
    ThermalSource source() const { return _source; }
    float temperatureC() const { return _temperature; }
    bool hasTemperature() const { return _source != THERMAL_SRC_NONE; }
    bool isThrottling() const { return _throttled; }
    uint16_t throttleEvents() const { return _throttleEvents; }

    void setEnabled(bool enable);
    bool isEnabled() const { return _enabled; }

    // Compute-slice cooperation. Long scans call tick() so the RTOS and the
    // task watchdog both get a turn.
    void beginSlice();
    void tick();
    uint32_t lastSliceMs() const { return _lastSliceMs; }

    // Duty estimate: percentage of wall time spent inside compute slices.
    uint8_t dutyPercent() const;

    static const char* stateName(ThermalState s);
    static const char* sourceName(ThermalSource s);

    String report() const;

private:
    Telemetry* _telemetry;
    bool _enabled;
    ThermalState _state;
    ThermalSource _source;
    float _temperature;
    bool _throttled;
    uint16_t _throttleEvents;
    uint32_t _originalCpuMhz;

    uint32_t _sliceStartMs;
    uint32_t _lastSliceMs;
    uint32_t _busyMsAccum;
    uint32_t _windowStartMs;
    uint8_t _dutyPercent;

    void applyPolicy();
};

#endif // AMELTECH_THERMAL_GUARD_H
