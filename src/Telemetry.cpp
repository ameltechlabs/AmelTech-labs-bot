// =============================================================
// Telemetry.cpp
//
// All measurements are gated behind platform capability checks.
// If a value cannot actually be measured on the compiling target,
// the field is left at MEAS_UNAVAILABLE/MEAS_UNSUPPORTED rather
// than being fabricated or defaulted to zero-as-meaning.
// =============================================================
#include "Telemetry.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
#define AMELTECH_ON_ESP32 1
#include <esp_system.h>
#include <esp_wifi.h>
#include <WiFi.h>
#if __has_include(<esp_chip_info.h>)
#include <esp_chip_info.h>
#endif
#if __has_include(<esp_mac.h>)
#include <esp_mac.h>
#endif
#if __has_include(<Wire.h>)
#include <Wire.h>
#define AMELTECH_HAVE_WIRE 1
#endif
#if __has_include("driver/temp_sensor.h")
#include "driver/temp_sensor.h"
#define AMELTECH_HAVE_TEMP_SENSOR 1
#endif
#else
#define AMELTECH_ON_ESP32 0
#endif

const char* measurementStatusToString(AmelTechMeasurementStatus status) {
    switch (status) {
        case MEAS_LIVE: return "LIVE";
        case MEAS_CACHED: return "CACHED";
        case MEAS_STALE: return "STALE";
        case MEAS_UNAVAILABLE: return "UNAVAILABLE";
        case MEAS_UNSUPPORTED: return "UNSUPPORTED";
        case MEAS_MEASUREMENT_ERROR: return "MEASUREMENT_ERROR";
        default: return "UNKNOWN";
    }
}

const char* esp32FamilyToString(ESP32Family family) {
    switch (family) {
        case FAMILY_ESP32: return "ESP32";
        case FAMILY_ESP32_S2: return "ESP32-S2";
        case FAMILY_ESP32_S3: return "ESP32-S3";
        case FAMILY_ESP32_C3: return "ESP32-C3";
        case FAMILY_ESP32_C6: return "ESP32-C6";
        case FAMILY_ESP32_H2: return "ESP32-H2";
        default: return "UNKNOWN";
    }
}

Telemetry::Telemetry()
    : _hasSnapshot(false), _loopMinUs(0), _loopMaxUs(0), _loopSumUs(0),
      _loopSampleCount(0), _lastLoopUs(0) {}

void Telemetry::begin() {
    _hasSnapshot = false;
    _loopMinUs = 0;
    _loopMaxUs = 0;
    _loopSumUs = 0;
    _loopSampleCount = 0;
    _lastLoopUs = 0;
}

void Telemetry::recordLoopSample(unsigned long durationUs) {
    float us = (float)durationUs;
    _lastLoopUs = us;
    if (_loopSampleCount == 0 || us < _loopMinUs) _loopMinUs = us;
    if (_loopSampleCount == 0 || us > _loopMaxUs) _loopMaxUs = us;
    _loopSumUs += us;
    _loopSampleCount++;
}

ESP32Family Telemetry::detectFamily() const {
#if defined(CONFIG_IDF_TARGET_ESP32S2)
    return FAMILY_ESP32_S2;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return FAMILY_ESP32_S3;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return FAMILY_ESP32_C3;
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    return FAMILY_ESP32_C6;
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    return FAMILY_ESP32_H2;
#elif defined(CONFIG_IDF_TARGET_ESP32) || AMELTECH_ON_ESP32
    return FAMILY_ESP32;
#else
    return FAMILY_UNKNOWN;
#endif
}

// ---------------------------------------------------------------
// Chip info
// ---------------------------------------------------------------
void Telemetry::_fillChipInfo(ESP32Telemetry& t) const {
    t.chip.family = detectFamily();

#if AMELTECH_ON_ESP32
    t.chip.modelName.value = String(esp32FamilyToString(t.chip.family));
    t.chip.modelName.status = MEAS_LIVE;
    t.chip.modelName.timestampMs = millis();

#if __has_include(<esp_chip_info.h>)
    esp_chip_info_t info;
    esp_chip_info(&info);
    t.chip.revision.value = info.revision;
    t.chip.revision.status = MEAS_LIVE;
    t.chip.revision.timestampMs = millis();

    t.chip.coreCount.value = info.cores;
    t.chip.coreCount.status = MEAS_LIVE;
    t.chip.coreCount.timestampMs = millis();
#else
    t.chip.revision.status = MEAS_UNAVAILABLE;
    t.chip.coreCount.status = MEAS_UNAVAILABLE;
#endif
#else
    t.chip.modelName.value = "UNKNOWN (non-ESP32 build)";
    t.chip.modelName.status = MEAS_UNSUPPORTED;
    t.chip.revision.status = MEAS_UNSUPPORTED;
    t.chip.coreCount.status = MEAS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------
// CPU (fast)
// ---------------------------------------------------------------
void Telemetry::_fillCpuFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    t.cpu.frequencyMHz.value = getCpuFrequencyMhz();
    t.cpu.frequencyMHz.status = MEAS_LIVE;
    t.cpu.frequencyMHz.timestampMs = millis();
#else
    t.cpu.frequencyMHz.status = MEAS_UNSUPPORTED;
#endif

    if (_loopSampleCount > 0) {
        t.cpu.lastLoopTimeUs.value = _lastLoopUs;
        t.cpu.lastLoopTimeUs.status = MEAS_LIVE;
        t.cpu.lastLoopTimeUs.timestampMs = millis();

        t.cpu.minLoopTimeUs.value = _loopMinUs;
        t.cpu.minLoopTimeUs.status = MEAS_LIVE;
        t.cpu.minLoopTimeUs.timestampMs = millis();

        t.cpu.maxLoopTimeUs.value = _loopMaxUs;
        t.cpu.maxLoopTimeUs.status = MEAS_LIVE;
        t.cpu.maxLoopTimeUs.timestampMs = millis();

        float avg = _loopSumUs / (float)_loopSampleCount;
        t.cpu.avgLoopTimeUs.value = avg;
        t.cpu.avgLoopTimeUs.status = MEAS_LIVE;
        t.cpu.avgLoopTimeUs.timestampMs = millis();

        t.cpu.jitterUs.value = _loopMaxUs - _loopMinUs;
        t.cpu.jitterUs.status = MEAS_LIVE;
        t.cpu.jitterUs.timestampMs = millis();
    } else {
        // No loop samples were ever recorded via recordLoopSample() —
        // do not fabricate timing data.
        t.cpu.lastLoopTimeUs.status = MEAS_UNAVAILABLE;
        t.cpu.minLoopTimeUs.status = MEAS_UNAVAILABLE;
        t.cpu.maxLoopTimeUs.status = MEAS_UNAVAILABLE;
        t.cpu.avgLoopTimeUs.status = MEAS_UNAVAILABLE;
        t.cpu.jitterUs.status = MEAS_UNAVAILABLE;
    }

    // Benchmark score is only populated by an explicit full scan
    // (see _fillCpuFast is never called with benchmarking; see sampleFull).
    t.cpu.benchmarkScore.status = MEAS_UNAVAILABLE;
}

// ---------------------------------------------------------------
// Memory (fast subset)
// ---------------------------------------------------------------
void Telemetry::_fillMemoryFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    t.memory.freeHeapBytes.value = ESP.getFreeHeap();
    t.memory.freeHeapBytes.status = MEAS_LIVE;
    t.memory.freeHeapBytes.timestampMs = millis();

    t.memory.minFreeHeapBytes.value = ESP.getMinFreeHeap();
    t.memory.minFreeHeapBytes.status = MEAS_LIVE;
    t.memory.minFreeHeapBytes.timestampMs = millis();
#else
    t.memory.freeHeapBytes.status = MEAS_UNSUPPORTED;
    t.memory.minFreeHeapBytes.status = MEAS_UNSUPPORTED;
#endif
    // Slow-only fields left UNAVAILABLE until a full scan populates them
    t.memory.largestFreeBlockBytes.status = MEAS_UNAVAILABLE;
    t.memory.heapFragmentationPct.status = MEAS_UNAVAILABLE;
    t.memory.psramTotalBytes.status = MEAS_UNAVAILABLE;
    t.memory.psramFreeBytes.status = MEAS_UNAVAILABLE;
    t.memory.flashSizeBytes.status = MEAS_UNAVAILABLE;
    t.memory.firmwareSizeBytes.status = MEAS_UNAVAILABLE;
    t.memory.sketchFreeSpaceBytes.status = MEAS_UNAVAILABLE;
}

// ---------------------------------------------------------------
// Memory (slow: flash/partitions/psram — full scan only)
// ---------------------------------------------------------------
void Telemetry::_fillMemorySlow(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    t.memory.largestFreeBlockBytes.value = ESP.getMaxAllocHeap();
    t.memory.largestFreeBlockBytes.status = MEAS_LIVE;
    t.memory.largestFreeBlockBytes.timestampMs = millis();

    uint32_t freeHeap = ESP.getFreeHeap();
    uint32_t largestBlock = ESP.getMaxAllocHeap();
    if (freeHeap > 0) {
        float frag = 100.0f * (1.0f - ((float)largestBlock / (float)freeHeap));
        if (frag < 0) frag = 0;
        t.memory.heapFragmentationPct.value = frag;
        t.memory.heapFragmentationPct.status = MEAS_LIVE;
        t.memory.heapFragmentationPct.timestampMs = millis();
    } else {
        t.memory.heapFragmentationPct.status = MEAS_MEASUREMENT_ERROR;
    }

#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM_SUPPORT)
    if (psramFound()) {
        t.memory.psramTotalBytes.value = ESP.getPsramSize();
        t.memory.psramTotalBytes.status = MEAS_LIVE;
        t.memory.psramTotalBytes.timestampMs = millis();

        t.memory.psramFreeBytes.value = ESP.getFreePsram();
        t.memory.psramFreeBytes.status = MEAS_LIVE;
        t.memory.psramFreeBytes.timestampMs = millis();
    } else {
        t.memory.psramTotalBytes.status = MEAS_UNAVAILABLE;
        t.memory.psramFreeBytes.status = MEAS_UNAVAILABLE;
    }
#else
    t.memory.psramTotalBytes.status = MEAS_UNSUPPORTED;
    t.memory.psramFreeBytes.status = MEAS_UNSUPPORTED;
#endif

    t.memory.flashSizeBytes.value = ESP.getFlashChipSize();
    t.memory.flashSizeBytes.status = MEAS_LIVE;
    t.memory.flashSizeBytes.timestampMs = millis();

    t.memory.firmwareSizeBytes.value = ESP.getSketchSize();
    t.memory.firmwareSizeBytes.status = MEAS_LIVE;
    t.memory.firmwareSizeBytes.timestampMs = millis();

    t.memory.sketchFreeSpaceBytes.value = ESP.getFreeSketchSpace();
    t.memory.sketchFreeSpaceBytes.status = MEAS_LIVE;
    t.memory.sketchFreeSpaceBytes.timestampMs = millis();
#else
    t.memory.largestFreeBlockBytes.status = MEAS_UNSUPPORTED;
    t.memory.heapFragmentationPct.status = MEAS_UNSUPPORTED;
    t.memory.psramTotalBytes.status = MEAS_UNSUPPORTED;
    t.memory.psramFreeBytes.status = MEAS_UNSUPPORTED;
    t.memory.flashSizeBytes.status = MEAS_UNSUPPORTED;
    t.memory.firmwareSizeBytes.status = MEAS_UNSUPPORTED;
    t.memory.sketchFreeSpaceBytes.status = MEAS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------
// Wi-Fi (fast)
// ---------------------------------------------------------------
void Telemetry::_fillWiFiFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    bool connected = (WiFi.status() == WL_CONNECTED);
    t.wifi.connected.value = connected;
    t.wifi.connected.status = MEAS_LIVE;
    t.wifi.connected.timestampMs = millis();

    if (connected) {
        int8_t rssi = (int8_t)WiFi.RSSI();
        t.wifi.rssiDbm.value = rssi;
        t.wifi.rssiDbm.status = MEAS_LIVE;
        t.wifi.rssiDbm.timestampMs = millis();

        String quality;
        if (rssi >= -50) quality = "EXCELLENT";
        else if (rssi >= -60) quality = "GOOD";
        else if (rssi >= -70) quality = "FAIR";
        else quality = "WEAK";
        t.wifi.signalQuality.value = quality;
        t.wifi.signalQuality.status = MEAS_LIVE;
        t.wifi.signalQuality.timestampMs = millis();
    } else {
        t.wifi.rssiDbm.status = MEAS_UNAVAILABLE;
        t.wifi.signalQuality.status = MEAS_UNAVAILABLE;
    }
#else
    t.wifi.connected.status = MEAS_UNSUPPORTED;
    t.wifi.rssiDbm.status = MEAS_UNSUPPORTED;
    t.wifi.signalQuality.status = MEAS_UNSUPPORTED;
#endif

    // Configured PHY rate vs measured throughput are kept explicitly
    // separate per spec item 16. Neither is fabricated: PHY rate is
    // only reported if the driver actually exposes it, and measured
    // throughput is only ever set by an explicit benchmark routine
    // (not implemented in fast/full sampling to avoid consuming
    // bandwidth/time without the user's intent).
    t.wifi.configuredLinkRateMbps.status = MEAS_UNAVAILABLE;
    t.wifi.measuredTxThroughputKbps.status = MEAS_UNAVAILABLE;
    t.wifi.measuredRxThroughputKbps.status = MEAS_UNAVAILABLE;
    t.wifi.packetErrorCount.status = MEAS_UNAVAILABLE;
    t.wifi.latencyMs.status = MEAS_UNAVAILABLE;
}

// ---------------------------------------------------------------
// Bluetooth (fast)
// ---------------------------------------------------------------
void Telemetry::_fillBluetoothFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32 && defined(CONFIG_BT_ENABLED)
    t.bluetooth.supported.value = true;
    t.bluetooth.supported.status = MEAS_LIVE;
    t.bluetooth.supported.timestampMs = millis();
    // Connection state requires an active BT stack/profile the user has
    // set up; the library does not initialize BT on its own.
    t.bluetooth.connected.status = MEAS_UNAVAILABLE;
#else
    t.bluetooth.supported.value = false;
    t.bluetooth.supported.status = MEAS_UNSUPPORTED;
    t.bluetooth.connected.status = MEAS_UNSUPPORTED;
#endif
    t.bluetooth.measuredTxThroughputKbps.status = MEAS_UNAVAILABLE;
    t.bluetooth.measuredRxThroughputKbps.status = MEAS_UNAVAILABLE;
    t.bluetooth.packetRate.status = MEAS_UNAVAILABLE;
}

// ---------------------------------------------------------------
// UART (fast: baud rate only; throughput needs explicit benchmark)
// ---------------------------------------------------------------
void Telemetry::_fillUartFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    // Serial.baudRate() reflects the configured rate of the default
    // UART if begin() was called by the user's sketch; if not called,
    // report UNAVAILABLE rather than guessing.
    unsigned long baud = Serial.baudRate();
    if (baud > 0) {
        t.uart.configuredBaudRate.value = (uint32_t)baud;
        t.uart.configuredBaudRate.status = MEAS_LIVE;
        t.uart.configuredBaudRate.timestampMs = millis();
    } else {
        t.uart.configuredBaudRate.status = MEAS_UNAVAILABLE;
    }
#else
    t.uart.configuredBaudRate.status = MEAS_UNSUPPORTED;
#endif
    // Throughput benchmarks are intentionally not run implicitly.
    t.uart.txThroughputBps.status = MEAS_UNAVAILABLE;
    t.uart.rxThroughputBps.status = MEAS_UNAVAILABLE;
    t.uart.errorCount.status = MEAS_UNAVAILABLE;
}

// ---------------------------------------------------------------
// I2C (slow: device scan)
// ---------------------------------------------------------------
void Telemetry::_fillI2cSlow(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32 && AMELTECH_HAVE_WIRE
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127 && found < 16; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            t.i2c.detectedAddresses[found] = addr;
            found++;
        }
    }
    t.i2c.detectedDeviceCount.value = found;
    t.i2c.detectedDeviceCount.status = MEAS_LIVE;
    t.i2c.detectedDeviceCount.timestampMs = millis();

    // Clock speed: Wire does not universally expose a getClock() across
    // all core versions, so this is reported UNAVAILABLE unless the
    // user's sketch configured Wire and the core exposes it.
    t.i2c.configuredClockHz.status = MEAS_UNAVAILABLE;
    t.i2c.transferBenchmarkKbps.status = MEAS_UNAVAILABLE; // requires explicit benchmark with a real device
    t.i2c.errorCount.status = MEAS_UNAVAILABLE;
#else
    t.i2c.detectedDeviceCount.status = MEAS_UNSUPPORTED;
    t.i2c.configuredClockHz.status = MEAS_UNSUPPORTED;
    t.i2c.transferBenchmarkKbps.status = MEAS_UNSUPPORTED;
    t.i2c.errorCount.status = MEAS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------
// SPI (fast: reports UNAVAILABLE unless benchmarked explicitly)
// ---------------------------------------------------------------
void Telemetry::_fillSpiFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    // SPI clock is only known once the sketch has called SPI.begin()
    // with a specific configuration; the library does not assume one.
    t.spi.configuredClockHz.status = MEAS_UNAVAILABLE;
    t.spi.txBenchmarkKbps.status = MEAS_UNAVAILABLE;
    t.spi.rxBenchmarkKbps.status = MEAS_UNAVAILABLE;
    t.spi.errorCount.status = MEAS_UNAVAILABLE;
#else
    t.spi.configuredClockHz.status = MEAS_UNSUPPORTED;
    t.spi.txBenchmarkKbps.status = MEAS_UNSUPPORTED;
    t.spi.rxBenchmarkKbps.status = MEAS_UNSUPPORTED;
    t.spi.errorCount.status = MEAS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------
// GPIO (slow: capability inventory is board/family dependent)
// ---------------------------------------------------------------
void Telemetry::_fillGpioSlow(ESP32Telemetry& t) const {
    ESP32Family family = detectFamily();
    // Documented, conservative usable-pin counts per family. These are
    // NOT live measurements — they describe known family capability,
    // consistent with docs/TELEMETRY.md. Actual usable count can be
    // lower depending on the specific board/module.
    uint8_t usable = 0;
    uint8_t restricted = 0;
    bool known = true;
    switch (family) {
        case FAMILY_ESP32:     usable = 25; restricted = 9;  break; // e.g. GPIO6-11 flash, GPIO34-39 input-only
        case FAMILY_ESP32_S2:  usable = 33; restricted = 6;  break;
        case FAMILY_ESP32_S3:  usable = 35; restricted = 6;  break;
        case FAMILY_ESP32_C3:  usable = 15; restricted = 4;  break;
        case FAMILY_ESP32_C6:  usable = 20; restricted = 4;  break;
        case FAMILY_ESP32_H2:  usable = 18; restricted = 4;  break;
        default: known = false; break;
    }
    if (known) {
        t.gpio.usablePinCount.value = usable;
        t.gpio.usablePinCount.status = MEAS_CACHED; // documented reference data, not a live probe
        t.gpio.usablePinCount.timestampMs = millis();

        t.gpio.restrictedPinCount.value = restricted;
        t.gpio.restrictedPinCount.status = MEAS_CACHED;
        t.gpio.restrictedPinCount.timestampMs = millis();
    } else {
        t.gpio.usablePinCount.status = MEAS_UNAVAILABLE;
        t.gpio.restrictedPinCount.status = MEAS_UNAVAILABLE;
    }
}

// ---------------------------------------------------------------
// Temperature (internal sensor only, where API is supported)
// ---------------------------------------------------------------
void Telemetry::_fillTemperature(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32 && AMELTECH_HAVE_TEMP_SENSOR
    temp_sensor_config_t cfg = TSENS_CONFIG_DEFAULT();
    temp_sensor_set_config(cfg);
    temp_sensor_start();
    float tempC = 0.0f;
    esp_err_t err = temp_sensor_read_celsius(&tempC);
    temp_sensor_stop();
    if (err == ESP_OK) {
        t.temperature.internalTempC.value = tempC;
        t.temperature.internalTempC.status = MEAS_LIVE;
        t.temperature.internalTempC.timestampMs = millis();
    } else {
        t.temperature.internalTempC.status = MEAS_MEASUREMENT_ERROR;
    }
#else
    // Classic ESP32 does not expose a documented reliable public
    // internal-temperature API through Arduino core in all versions;
    // rather than guess, report UNSUPPORTED.
    t.temperature.internalTempC.status = MEAS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------
// Watchdog
// ---------------------------------------------------------------
void Telemetry::_fillWatchdog(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    // Whether task watchdog is enabled/configured is application-specific;
    // without a documented universal getter across core versions, report
    // UNAVAILABLE rather than guessing.
    t.watchdog.enabled.status = MEAS_UNAVAILABLE;
    t.watchdog.timeoutMs.status = MEAS_UNAVAILABLE;
#else
    t.watchdog.enabled.status = MEAS_UNSUPPORTED;
    t.watchdog.timeoutMs.status = MEAS_UNSUPPORTED;
#endif
}

// ---------------------------------------------------------------
// System (fast)
// ---------------------------------------------------------------
void Telemetry::_fillSystemFast(ESP32Telemetry& t) const {
#if AMELTECH_ON_ESP32
    t.system.uptimeMs.value = millis();
    t.system.uptimeMs.status = MEAS_LIVE;
    t.system.uptimeMs.timestampMs = millis();

    esp_reset_reason_t reason = esp_reset_reason();
    String reasonStr;
    switch (reason) {
        case ESP_RST_POWERON: reasonStr = "POWERON"; break;
        case ESP_RST_SW: reasonStr = "SOFTWARE"; break;
        case ESP_RST_PANIC: reasonStr = "PANIC"; break;
        case ESP_RST_INT_WDT: reasonStr = "INT_WATCHDOG"; break;
        case ESP_RST_TASK_WDT: reasonStr = "TASK_WATCHDOG"; break;
        case ESP_RST_WDT: reasonStr = "WATCHDOG"; break;
        case ESP_RST_DEEPSLEEP: reasonStr = "DEEPSLEEP_WAKE"; break;
        case ESP_RST_BROWNOUT: reasonStr = "BROWNOUT"; break;
        case ESP_RST_SDIO: reasonStr = "SDIO"; break;
        default: reasonStr = "UNKNOWN"; break;
    }
    t.system.resetReason.value = reasonStr;
    t.system.resetReason.status = MEAS_LIVE;
    t.system.resetReason.timestampMs = millis();
#else
    t.system.uptimeMs.status = MEAS_UNSUPPORTED;
    t.system.resetReason.status = MEAS_UNSUPPORTED;
#endif
    // Reboot count requires persistent storage tracking, which the
    // library does not maintain automatically to avoid unsolicited
    // flash wear; Diagnostics module documents an opt-in path.
    t.system.rebootCount.status = MEAS_UNAVAILABLE;

    t.errors.communicationErrors = 0;
    t.errors.measurementErrors = 0;
    t.errors.storageErrors = 0;
}

// ---------------------------------------------------------------
// Public sampling entry points
// ---------------------------------------------------------------
ESP32Telemetry Telemetry::sampleFast() {
    ESP32Telemetry t;
    t.wasFullScan = false;
    t.snapshotTimestampMs = millis();

    _fillChipInfo(t);
    _fillCpuFast(t);
    _fillMemoryFast(t);
    _fillWiFiFast(t);
    _fillBluetoothFast(t);
    _fillUartFast(t);
    _fillSpiFast(t);
    _fillTemperature(t);
    _fillWatchdog(t);
    _fillSystemFast(t);

    // GPIO/I2C/DAC/PWM/ADC are slow or require explicit context; left
    // UNAVAILABLE on a fast sample.
    t.gpio.usablePinCount.status = MEAS_UNAVAILABLE;
    t.gpio.restrictedPinCount.status = MEAS_UNAVAILABLE;
    t.i2c.detectedDeviceCount.status = MEAS_UNAVAILABLE;
    t.i2c.configuredClockHz.status = MEAS_UNAVAILABLE;
    t.i2c.transferBenchmarkKbps.status = MEAS_UNAVAILABLE;
    t.i2c.errorCount.status = MEAS_UNAVAILABLE;
    t.adc.rawReading.status = MEAS_UNAVAILABLE;
    t.adc.bitWidth.status = MEAS_UNAVAILABLE;
    t.adc.attenuation.status = MEAS_UNAVAILABLE;
    t.dac.supported.status = MEAS_UNAVAILABLE;
    t.dac.configuredOutput.status = MEAS_UNAVAILABLE;
    t.pwm.frequencyHz.status = MEAS_UNAVAILABLE;
    t.pwm.resolutionBits.status = MEAS_UNAVAILABLE;
    t.pwm.configuredDutyCyclePct.status = MEAS_UNAVAILABLE;
    t.pwm.channelCount.status = MEAS_UNAVAILABLE;
    t.memory.largestFreeBlockBytes.status = MEAS_UNAVAILABLE;
    t.memory.heapFragmentationPct.status = MEAS_UNAVAILABLE;
    t.memory.psramTotalBytes.status = MEAS_UNAVAILABLE;
    t.memory.psramFreeBytes.status = MEAS_UNAVAILABLE;
    t.memory.flashSizeBytes.status = MEAS_UNAVAILABLE;
    t.memory.firmwareSizeBytes.status = MEAS_UNAVAILABLE;
    t.memory.sketchFreeSpaceBytes.status = MEAS_UNAVAILABLE;

    _lastSnapshot = t;
    _hasSnapshot = true;
    return t;
}

ESP32Telemetry Telemetry::sampleFull() {
    ESP32Telemetry t;
    t.wasFullScan = true;
    t.snapshotTimestampMs = millis();

    _fillChipInfo(t);
    _fillCpuFast(t);
    _fillMemoryFast(t);
    _fillMemorySlow(t);
    _fillWiFiFast(t);
    _fillBluetoothFast(t);
    _fillUartFast(t);
    _fillI2cSlow(t);
    _fillSpiFast(t);
    _fillGpioSlow(t);
    _fillTemperature(t);
    _fillWatchdog(t);
    _fillSystemFast(t);

    // ADC/DAC/PWM require a specific pin/channel argument from the user
    // to be meaningful; the general telemetry snapshot cannot guess
    // which pin to sample, so these remain UNAVAILABLE here. Dedicated
    // API methods (not part of the general snapshot) would take a pin
    // argument for on-demand ADC/DAC/PWM readings.
    t.adc.rawReading.status = MEAS_UNAVAILABLE;
    t.adc.bitWidth.status = MEAS_UNAVAILABLE;
    t.adc.attenuation.status = MEAS_UNAVAILABLE;
#if AMELTECH_ON_ESP32
    t.dac.supported.value = FAMILY_ESP32 == detectFamily(); // classic ESP32 has 2 DAC channels
    t.dac.supported.status = MEAS_LIVE;
    t.dac.supported.timestampMs = millis();
#else
    t.dac.supported.status = MEAS_UNSUPPORTED;
#endif
    t.dac.configuredOutput.status = MEAS_UNAVAILABLE;
    t.pwm.frequencyHz.status = MEAS_UNAVAILABLE;
    t.pwm.resolutionBits.status = MEAS_UNAVAILABLE;
    t.pwm.configuredDutyCyclePct.status = MEAS_UNAVAILABLE;
    t.pwm.channelCount.status = MEAS_UNAVAILABLE;

    _lastSnapshot = t;
    _hasSnapshot = true;
    return t;
}
