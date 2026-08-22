// =============================================================
// Telemetry.h
//
// Structured ESP32 hardware telemetry model. Every measurable
// field carries an explicit status (LIVE/CACHED/STALE/UNAVAILABLE/
// UNSUPPORTED/MEASUREMENT_ERROR) — zero is NEVER used to mean
// "unsupported" or "not measured".
//
// Two-speed telemetry (spec item 28):
//   - fast fields are cheap and safe to sample on every ask()
//   - slow fields (flash/partition/GPIO inventory/comm benchmarks)
//     are only gathered on an explicit full scan
// =============================================================
#ifndef AMELTECH_TELEMETRY_H
#define AMELTECH_TELEMETRY_H

#include <Arduino.h>

enum AmelTechMeasurementStatus {
    MEAS_LIVE = 0,
    MEAS_CACHED,
    MEAS_STALE,
    MEAS_UNAVAILABLE,
    MEAS_UNSUPPORTED,
    MEAS_MEASUREMENT_ERROR
};

const char* measurementStatusToString(AmelTechMeasurementStatus status);

// Generic scalar measurement wrapper used throughout the telemetry model.
template <typename T>
struct Measurement {
    T value;
    AmelTechMeasurementStatus status;
    unsigned long timestampMs; // millis() at time of measurement; 0 if never measured

    Measurement() : value(T()), status(MEAS_UNAVAILABLE), timestampMs(0) {}
};

// -------------------------------------------------------------
// Detected chip family (spec item 12)
// -------------------------------------------------------------
enum ESP32Family {
    FAMILY_ESP32 = 0,
    FAMILY_ESP32_S2,
    FAMILY_ESP32_S3,
    FAMILY_ESP32_C3,
    FAMILY_ESP32_C6,
    FAMILY_ESP32_H2,
    FAMILY_UNKNOWN
};

const char* esp32FamilyToString(ESP32Family family);

// -------------------------------------------------------------
// Sub-structures
// -------------------------------------------------------------
struct ChipInfo {
    ESP32Family family;
    Measurement<String> modelName;
    Measurement<uint8_t> revision;
    Measurement<uint8_t> coreCount;
};

struct CpuTelemetry {
    Measurement<uint32_t> frequencyMHz;
    Measurement<float> lastLoopTimeUs;
    Measurement<float> minLoopTimeUs;
    Measurement<float> avgLoopTimeUs;
    Measurement<float> maxLoopTimeUs;
    Measurement<float> jitterUs;
    Measurement<uint32_t> benchmarkScore; // arbitrary-unit busy-loop benchmark, documented in TELEMETRY.md
};

struct MemoryTelemetry {
    Measurement<uint32_t> freeHeapBytes;
    Measurement<uint32_t> minFreeHeapBytes;
    Measurement<uint32_t> largestFreeBlockBytes;
    Measurement<float> heapFragmentationPct;
    Measurement<uint32_t> psramTotalBytes;
    Measurement<uint32_t> psramFreeBytes;
    Measurement<uint32_t> flashSizeBytes;
    Measurement<uint32_t> firmwareSizeBytes;
    Measurement<uint32_t> sketchFreeSpaceBytes;
};

struct WiFiTelemetry {
    Measurement<bool> connected;
    Measurement<int8_t> rssiDbm;
    Measurement<String> signalQuality; // derived human label from RSSI thresholds
    Measurement<float> configuredLinkRateMbps; // PHY rate as reported by driver, NOT measured throughput
    Measurement<float> measuredTxThroughputKbps; // only populated by an explicit throughput benchmark
    Measurement<float> measuredRxThroughputKbps;
    Measurement<uint32_t> packetErrorCount;
    Measurement<float> latencyMs; // measured via local loopback/DNS timing if performed
};

struct BluetoothTelemetry {
    Measurement<bool> supported;
    Measurement<bool> connected;
    Measurement<float> measuredTxThroughputKbps;
    Measurement<float> measuredRxThroughputKbps;
    Measurement<uint32_t> packetRate;
};

struct UartTelemetry {
    Measurement<uint32_t> configuredBaudRate;
    Measurement<float> txThroughputBps; // benchmarked, not equal to baud rate
    Measurement<float> rxThroughputBps;
    Measurement<uint32_t> errorCount;
};

struct I2cTelemetry {
    Measurement<uint32_t> configuredClockHz;
    Measurement<uint8_t> detectedDeviceCount;
    uint8_t detectedAddresses[16];
    Measurement<float> transferBenchmarkKbps;
    Measurement<uint32_t> errorCount;
};

struct SpiTelemetry {
    Measurement<uint32_t> configuredClockHz;
    Measurement<float> txBenchmarkKbps;
    Measurement<float> rxBenchmarkKbps;
    Measurement<uint32_t> errorCount;
};

struct GpioTelemetry {
    Measurement<uint8_t> usablePinCount; // board/family dependent, documented
    Measurement<uint8_t> restrictedPinCount;
};

struct AdcTelemetry {
    Measurement<uint16_t> rawReading; // only populated if a pin was explicitly sampled
    Measurement<uint8_t> bitWidth;
    Measurement<String> attenuation;
};

struct DacTelemetry {
    Measurement<bool> supported;
    Measurement<uint8_t> configuredOutput;
};

struct PwmTelemetry {
    Measurement<uint32_t> frequencyHz;
    Measurement<uint8_t> resolutionBits;
    Measurement<float> configuredDutyCyclePct;
    Measurement<uint8_t> channelCount;
};

struct TemperatureTelemetry {
    Measurement<float> internalTempC; // only if platform exposes a supported API
};

struct WatchdogTelemetry {
    Measurement<bool> enabled;
    Measurement<uint32_t> timeoutMs;
};

struct SystemTelemetry {
    Measurement<unsigned long> uptimeMs;
    Measurement<String> resetReason;
    Measurement<uint32_t> rebootCount; // requires persistent storage; UNAVAILABLE otherwise
};

struct ErrorCounters {
    uint32_t communicationErrors;
    uint32_t measurementErrors;
    uint32_t storageErrors;
};

// -------------------------------------------------------------
// Top-level telemetry snapshot
// -------------------------------------------------------------
struct ESP32Telemetry {
    ChipInfo chip;
    CpuTelemetry cpu;
    MemoryTelemetry memory;
    WiFiTelemetry wifi;
    BluetoothTelemetry bluetooth;
    UartTelemetry uart;
    I2cTelemetry i2c;
    SpiTelemetry spi;
    GpioTelemetry gpio;
    AdcTelemetry adc;
    DacTelemetry dac;
    PwmTelemetry pwm;
    TemperatureTelemetry temperature;
    WatchdogTelemetry watchdog;
    SystemTelemetry system;
    ErrorCounters errors;

    bool wasFullScan;
    unsigned long snapshotTimestampMs;
};

// -------------------------------------------------------------
// Telemetry collector
// -------------------------------------------------------------
class Telemetry {
public:
    Telemetry();

    void begin();

    // Fast telemetry: cheap fields safe to call frequently (see spec item 28)
    ESP32Telemetry sampleFast();

    // Full/slow telemetry: expensive fields (flash, partitions, I2C scan, benchmarks)
    ESP32Telemetry sampleFull();

    // Records one loop() duration sample for CPU jitter/timing stats.
    // Call this once per loop() iteration if loop timing stats are desired.
    void recordLoopSample(unsigned long durationUs);

    ESP32Family detectFamily() const;

    const ESP32Telemetry& lastSnapshot() const { return _lastSnapshot; }

private:
    ESP32Telemetry _lastSnapshot;
    bool _hasSnapshot;

    // Loop timing ring stats (bounded)
    float _loopMinUs;
    float _loopMaxUs;
    float _loopSumUs;
    uint32_t _loopSampleCount;
    float _lastLoopUs;

    void _fillChipInfo(ESP32Telemetry& t) const;
    void _fillCpuFast(ESP32Telemetry& t) const;
    void _fillMemoryFast(ESP32Telemetry& t) const;
    void _fillMemorySlow(ESP32Telemetry& t) const;
    void _fillWiFiFast(ESP32Telemetry& t) const;
    void _fillBluetoothFast(ESP32Telemetry& t) const;
    void _fillUartFast(ESP32Telemetry& t) const;
    void _fillI2cSlow(ESP32Telemetry& t) const;
    void _fillSpiFast(ESP32Telemetry& t) const;
    void _fillGpioSlow(ESP32Telemetry& t) const;
    void _fillTemperature(ESP32Telemetry& t) const;
    void _fillWatchdog(ESP32Telemetry& t) const;
    void _fillSystemFast(ESP32Telemetry& t) const;
};

#endif // AMELTECH_TELEMETRY_H
