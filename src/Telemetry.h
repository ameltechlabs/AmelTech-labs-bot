/*
 * Telemetry.h
 * ---------------------------------------------------------------------------
 * ESP32 measurement model.
 *
 * Every field carries a MeasurementStatus alongside its value. A number is only
 * ever presented when it was actually measured; anything else is reported as
 * UNAVAILABLE, UNSUPPORTED, STALE or MEASUREMENT_ERROR. Nothing is invented,
 * and nothing is silently carried forward as if it were fresh.
 *
 * Reads are rate limited. Polling Wi-Fi and heap statistics in a tight loop
 * wastes cycles and generates heat for no new information, so repeated calls
 * inside the minimum interval return the cached values marked CACHED.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_TELEMETRY_H
#define AMELTECH_TELEMETRY_H

#include <Arduino.h>
#include "AmelTechConfig.h"

enum MeasurementStatus : uint8_t {
    MEAS_LIVE = 0,
    MEAS_CACHED,
    MEAS_STALE,
    MEAS_UNAVAILABLE,
    MEAS_UNSUPPORTED,
    MEAS_ERROR
};

struct MeasF { float value; MeasurementStatus status; };
struct MeasI { int32_t value; MeasurementStatus status; };
struct MeasU { uint32_t value; MeasurementStatus status; };
struct MeasB { bool value; MeasurementStatus status; };

struct ESP32Telemetry {
    // Chip identity
    char chipModel[24];
    MeasurementStatus chipModelStatus;
    uint8_t chipCores;
    MeasurementStatus chipCoresStatus;
    uint32_t chipRevision;
    MeasurementStatus chipRevisionStatus;
    uint64_t mac;
    MeasurementStatus macStatus;
    char sdkVersion[24];
    MeasurementStatus sdkVersionStatus;

    // CPU
    MeasU cpuFreqMhz;
    MeasU cycleCount;

    // Memory
    MeasU freeHeap;
    MeasU minFreeHeap;
    MeasU heapSize;
    MeasU largestFreeBlock;
    MeasU heapFragmentationPct;   // 0-100, derived
    MeasU psramSize;
    MeasU freePsram;
    MeasU flashSize;
    MeasU sketchSize;
    MeasU freeSketchSpace;

    // Wi-Fi
    MeasB wifiConnected;
    MeasI wifiRssi;
    MeasU wifiStatus;
    char wifiSsid[33];
    MeasurementStatus wifiSsidStatus;
    MeasU wifiTxThroughputBps;   // UNAVAILABLE unless the sketch measures it
    MeasU wifiRxThroughputBps;
    MeasU wifiDisconnectCount;

    // Bluetooth
    MeasB btEnabled;

    // System
    MeasU uptimeMs;
    MeasU resetReason;
    MeasF temperatureC;          // die temperature; UNSUPPORTED on many parts
    MeasU taskStackHighWater;

    // Ambient, supplied by SensorHub when a DHT is attached
    MeasF ambientTemperatureC;
    MeasF ambientHumidity;

    // Software health inputs
    MeasU errorCount;
    MeasU warningCount;
    MeasU loopLatencyMs;

    uint32_t lastFastUpdateMs;
    uint32_t lastFullUpdateMs;
};

class Telemetry {
public:
    Telemetry();

    void begin();

    // Cheap: heap, CPU, uptime, Wi-Fi (Wi-Fi on its own slower interval).
    void updateFast(bool force = false);
    // Everything, including flash geometry and the die temperature.
    void updateFull(bool force = false);

    const ESP32Telemetry& data() const { return _data; }
    ESP32Telemetry& data() { return _data; }

    MeasurementStatus lastStatus() const { return _lastStatus; }

    // Ambient values come from SensorHub; the status is passed through so a
    // failed sensor read cannot masquerade as a measurement.
    void setAmbient(float tempC, float humidity, MeasurementStatus status);
    void clearAmbient();

    // Software counters fed by the rest of the library.
    void noteError() { if (_errorCount < 0xFFFFFFFFUL) ++_errorCount; }
    void noteWarning() { if (_warningCount < 0xFFFFFFFFUL) ++_warningCount; }
    void noteWifiDisconnect() { ++_wifiDisconnects; }
    void noteLoopLatency(uint32_t ms);
    void resetCounters();

    static const char* statusToString(MeasurementStatus s);
    static const char* resetReasonToString(uint32_t reason);
    static bool statusIsUsable(MeasurementStatus s) {
        return s == MEAS_LIVE || s == MEAS_CACHED;
    }

private:
    ESP32Telemetry _data;
    MeasurementStatus _lastStatus;
    bool _begun;
    uint32_t _lastWifiReadMs;
    uint32_t _errorCount;
    uint32_t _warningCount;
    uint32_t _wifiDisconnects;
    uint32_t _loopLatencyEmaMs;
    bool _lastWifiConnected;
    bool _temperatureUsable;
    bool _temperatureProbed;

    void markAllUnsupported();
    void readChipInfo();
    void readMemory();
    void readWifi(bool force);
    void readTemperature();
    void readSystem();
    void ageCachedFields();
};

#endif // AMELTECH_TELEMETRY_H
