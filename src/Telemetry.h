/*
 * ESP32 Telemetry model
 * Every measurable field has value + status. Never fabricate.
 */

#ifndef AMELTECH_TELEMETRY_H
#define AMELTECH_TELEMETRY_H

#include <Arduino.h>

enum MeasurementStatus : uint8_t {
    MEAS_LIVE = 0,
    MEAS_CACHED,
    MEAS_STALE,
    MEAS_UNAVAILABLE,
    MEAS_UNSUPPORTED,
    MEAS_ERROR
};

struct MeasF {
    float value;
    MeasurementStatus status;
};

struct MeasI {
    int32_t value;
    MeasurementStatus status;
};

struct MeasU {
    uint32_t value;
    MeasurementStatus status;
};

struct MeasB {
    bool value;
    MeasurementStatus status;
};

struct ESP32Telemetry {
    // Chip / identity
    char chipModel[24];
    MeasurementStatus chipModelStatus;
    uint8_t chipCores;
    MeasurementStatus chipCoresStatus;
    uint32_t chipRevision;
    MeasurementStatus chipRevisionStatus;
    uint64_t mac;
    MeasurementStatus macStatus;

    // CPU
    MeasU cpuFreqMhz;
    MeasU cycleCount;

    // Memory
    MeasU freeHeap;
    MeasU minFreeHeap;
    MeasU heapSize;
    MeasU largestFreeBlock;
    MeasU psramSize;
    MeasU freePsram;
    MeasU flashSize;
    MeasU sketchSize;
    MeasU freeSketchSpace;

    // WiFi
    MeasB wifiConnected;
    MeasI wifiRssi;
    MeasU wifiStatus;  // wl_status style
    char wifiSsid[33];
    MeasurementStatus wifiSsidStatus;
    // Measured throughput not claimed unless actually measured
    MeasU wifiTxThroughputBps;  // UNAVAILABLE by default
    MeasU wifiRxThroughputBps;

    // Bluetooth
    MeasB btEnabled;
    // detailed BT metrics mostly UNSUPPORTED unless measured

    // System
    MeasU uptimeMs;
    MeasU resetReason;
    MeasF temperatureC;  // UNSUPPORTED on many chips

    // Errors / health inputs
    MeasU errorCount;

    // Timestamp of last fast update
    uint32_t lastUpdateMs;
};

class Telemetry {
public:
    Telemetry();

    void begin();
    void updateFast();   // cheap: heap, cpu freq, uptime, rssi
    void updateFull();   // slower: flash, partitions, scans, etc.

    const ESP32Telemetry& data() const { return _data; }
    ESP32Telemetry& data() { return _data; }

    MeasurementStatus lastStatus() const { return _lastStatus; }

    // Helpers
    static const char* statusToString(MeasurementStatus s);
    static const char* resetReasonToString(uint32_t reason);

private:
    ESP32Telemetry _data;
    MeasurementStatus _lastStatus;
    bool _begun;

    void clearUnsupported();
    void readChipInfo();
    void readMemory();
    void readWifi();
    void readTemperature();
    void readSystem();
};

#endif // AMELTECH_TELEMETRY_H
