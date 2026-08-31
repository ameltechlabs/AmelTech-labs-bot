#include "Telemetry.h"
#include "AmelTechLog.h"
#include <string.h>
#include <math.h>

#if defined(ESP32)
#include <esp_system.h>
#include <esp_chip_info.h>
#include <WiFi.h>
#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif
#endif

// ---------------------------------------------------------------------------
Telemetry::Telemetry()
    : _lastStatus(MEAS_UNAVAILABLE),
      _begun(false),
      _lastWifiReadMs(0),
      _errorCount(0),
      _warningCount(0),
      _wifiDisconnects(0),
      _loopLatencyEmaMs(0),
      _lastWifiConnected(false),
      _temperatureUsable(false),
      _temperatureProbed(false) {
    memset(&_data, 0, sizeof(_data));
    markAllUnsupported();
}

void Telemetry::markAllUnsupported() {
    auto setU = [](MeasU& m) { m.value = 0; m.status = MEAS_UNSUPPORTED; };
    auto setI = [](MeasI& m) { m.value = 0; m.status = MEAS_UNSUPPORTED; };
    auto setF = [](MeasF& m) { m.value = 0.0f; m.status = MEAS_UNSUPPORTED; };
    auto setB = [](MeasB& m) { m.value = false; m.status = MEAS_UNSUPPORTED; };

    _data.chipModel[0] = '\0';
    _data.chipModelStatus = MEAS_UNSUPPORTED;
    _data.chipCores = 0;
    _data.chipCoresStatus = MEAS_UNSUPPORTED;
    _data.chipRevision = 0;
    _data.chipRevisionStatus = MEAS_UNSUPPORTED;
    _data.mac = 0;
    _data.macStatus = MEAS_UNSUPPORTED;
    _data.sdkVersion[0] = '\0';
    _data.sdkVersionStatus = MEAS_UNSUPPORTED;

    setU(_data.cpuFreqMhz);
    setU(_data.cycleCount);
    setU(_data.freeHeap);
    setU(_data.minFreeHeap);
    setU(_data.heapSize);
    setU(_data.largestFreeBlock);
    setU(_data.heapFragmentationPct);
    setU(_data.psramSize);
    setU(_data.freePsram);
    setU(_data.flashSize);
    setU(_data.sketchSize);
    setU(_data.freeSketchSpace);

    setB(_data.wifiConnected);
    setI(_data.wifiRssi);
    setU(_data.wifiStatus);
    _data.wifiSsid[0] = '\0';
    _data.wifiSsidStatus = MEAS_UNSUPPORTED;
    setU(_data.wifiTxThroughputBps);
    setU(_data.wifiRxThroughputBps);
    setU(_data.wifiDisconnectCount);
    // Throughput is never guessed: it stays unavailable until measured.
    _data.wifiTxThroughputBps.status = MEAS_UNAVAILABLE;
    _data.wifiRxThroughputBps.status = MEAS_UNAVAILABLE;

    setB(_data.btEnabled);
    setU(_data.uptimeMs);
    setU(_data.resetReason);
    setF(_data.temperatureC);
    setU(_data.taskStackHighWater);

    setF(_data.ambientTemperatureC);
    setF(_data.ambientHumidity);
    _data.ambientTemperatureC.status = MEAS_UNAVAILABLE;
    _data.ambientHumidity.status = MEAS_UNAVAILABLE;

    setU(_data.errorCount);
    setU(_data.warningCount);
    setU(_data.loopLatencyMs);

    _data.lastFastUpdateMs = 0;
    _data.lastFullUpdateMs = 0;
}

const char* Telemetry::statusToString(MeasurementStatus s) {
    switch (s) {
        case MEAS_LIVE:        return "LIVE";
        case MEAS_CACHED:      return "CACHED";
        case MEAS_STALE:       return "STALE";
        case MEAS_UNAVAILABLE: return "UNAVAILABLE";
        case MEAS_UNSUPPORTED: return "UNSUPPORTED";
        case MEAS_ERROR:       return "MEASUREMENT_ERROR";
        default:               return "UNKNOWN";
    }
}

const char* Telemetry::resetReasonToString(uint32_t reason) {
#if defined(ESP32)
    switch ((esp_reset_reason_t)reason) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXTERNAL";
        case ESP_RST_SW:        return "SOFTWARE";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INTERRUPT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
#else
    (void)reason;
    return "UNSUPPORTED";
#endif
}

void Telemetry::begin() {
    markAllUnsupported();
#if defined(ESP32)
    readChipInfo();
    readSystem();
    readMemory();
    readWifi(true);
    readTemperature();
    _data.lastFullUpdateMs = millis();
    _data.lastFastUpdateMs = _data.lastFullUpdateMs;
    _lastStatus = MEAS_LIVE;
#else
    _lastStatus = MEAS_UNSUPPORTED;
#endif
    _begun = true;
}

void Telemetry::resetCounters() {
    _errorCount = 0;
    _warningCount = 0;
    _wifiDisconnects = 0;
    _loopLatencyEmaMs = 0;
}

void Telemetry::noteLoopLatency(uint32_t ms) {
    // Exponential moving average, alpha = 1/4, integer only.
    if (_loopLatencyEmaMs == 0) _loopLatencyEmaMs = ms;
    else _loopLatencyEmaMs = (_loopLatencyEmaMs * 3 + ms) / 4;
    _data.loopLatencyMs.value = _loopLatencyEmaMs;
    _data.loopLatencyMs.status = MEAS_LIVE;
}

void Telemetry::setAmbient(float tempC, float humidity, MeasurementStatus status) {
    _data.ambientTemperatureC.value = tempC;
    _data.ambientTemperatureC.status = status;
    _data.ambientHumidity.value = humidity;
    _data.ambientHumidity.status = status;
}

void Telemetry::clearAmbient() {
    _data.ambientTemperatureC.value = 0.0f;
    _data.ambientTemperatureC.status = MEAS_UNAVAILABLE;
    _data.ambientHumidity.value = 0.0f;
    _data.ambientHumidity.status = MEAS_UNAVAILABLE;
}

// ---------------------------------------------------------------------------
void Telemetry::readChipInfo() {
#if defined(ESP32)
    esp_chip_info_t info;
    esp_chip_info(&info);
    _data.chipCores = info.cores;
    _data.chipCoresStatus = MEAS_LIVE;
    _data.chipRevision = info.revision;
    _data.chipRevisionStatus = MEAS_LIVE;

    // ESP.getChipModel() reports the actual silicon and is present on every
    // supported core, so it is preferred over guessing from build defines.
    const char* model = ESP.getChipModel();
    if (model && model[0]) {
        strncpy(_data.chipModel, model, sizeof(_data.chipModel) - 1);
        _data.chipModel[sizeof(_data.chipModel) - 1] = '\0';
        _data.chipModelStatus = MEAS_LIVE;
    } else {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
        strncpy(_data.chipModel, "ESP32-S3", sizeof(_data.chipModel) - 1);
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
        strncpy(_data.chipModel, "ESP32-S2", sizeof(_data.chipModel) - 1);
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
        strncpy(_data.chipModel, "ESP32-C3", sizeof(_data.chipModel) - 1);
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
        strncpy(_data.chipModel, "ESP32-C6", sizeof(_data.chipModel) - 1);
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
        strncpy(_data.chipModel, "ESP32-H2", sizeof(_data.chipModel) - 1);
#else
        strncpy(_data.chipModel, "ESP32", sizeof(_data.chipModel) - 1);
#endif
        _data.chipModel[sizeof(_data.chipModel) - 1] = '\0';
        _data.chipModelStatus = MEAS_LIVE;
    }

    _data.mac = ESP.getEfuseMac();
    _data.macStatus = MEAS_LIVE;

    const char* sdk = ESP.getSdkVersion();
    if (sdk && sdk[0]) {
        strncpy(_data.sdkVersion, sdk, sizeof(_data.sdkVersion) - 1);
        _data.sdkVersion[sizeof(_data.sdkVersion) - 1] = '\0';
        _data.sdkVersionStatus = MEAS_LIVE;
    }
#endif
}

void Telemetry::readMemory() {
#if defined(ESP32)
    _data.freeHeap.value = ESP.getFreeHeap();
    _data.freeHeap.status = MEAS_LIVE;
    _data.minFreeHeap.value = ESP.getMinFreeHeap();
    _data.minFreeHeap.status = MEAS_LIVE;
    _data.heapSize.value = ESP.getHeapSize();
    _data.heapSize.status = MEAS_LIVE;
    _data.largestFreeBlock.value = ESP.getMaxAllocHeap();
    _data.largestFreeBlock.status = MEAS_LIVE;

    // Fragmentation: how much of the free heap is unusable for one big block.
    if (_data.freeHeap.value > 0 && _data.largestFreeBlock.status == MEAS_LIVE) {
        uint32_t frag = 100;
        if (_data.largestFreeBlock.value <= _data.freeHeap.value) {
            frag = 100u - (uint32_t)((uint64_t)_data.largestFreeBlock.value * 100u /
                                     _data.freeHeap.value);
        } else {
            frag = 0;
        }
        _data.heapFragmentationPct.value = frag;
        _data.heapFragmentationPct.status = MEAS_LIVE;
    } else {
        _data.heapFragmentationPct.status = MEAS_UNAVAILABLE;
    }

    if (psramFound()) {
        _data.psramSize.value = ESP.getPsramSize();
        _data.psramSize.status = MEAS_LIVE;
        _data.freePsram.value = ESP.getFreePsram();
        _data.freePsram.status = MEAS_LIVE;
    } else {
        _data.psramSize.status = MEAS_UNSUPPORTED;
        _data.freePsram.status = MEAS_UNSUPPORTED;
    }

    _data.flashSize.value = ESP.getFlashChipSize();
    _data.flashSize.status = MEAS_LIVE;
    _data.sketchSize.value = ESP.getSketchSize();
    _data.sketchSize.status = MEAS_LIVE;
    _data.freeSketchSpace.value = ESP.getFreeSketchSpace();
    _data.freeSketchSpace.status = MEAS_LIVE;

    _data.taskStackHighWater.value = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    _data.taskStackHighWater.status = MEAS_LIVE;
#endif
}

void Telemetry::readWifi(bool force) {
#if defined(ESP32)
    uint32_t now = millis();
    if (!force && _lastWifiReadMs != 0 &&
        (now - _lastWifiReadMs) < AMELTECH_TELEM_WIFI_MIN_INTERVAL_MS) {
        // Downgrade LIVE to CACHED so callers can tell the difference.
        if (_data.wifiConnected.status == MEAS_LIVE) _data.wifiConnected.status = MEAS_CACHED;
        if (_data.wifiRssi.status == MEAS_LIVE) _data.wifiRssi.status = MEAS_CACHED;
        return;
    }
    _lastWifiReadMs = now;

    wl_status_t st = WiFi.status();
    _data.wifiStatus.value = (uint32_t)st;
    _data.wifiStatus.status = MEAS_LIVE;

    bool connected = (st == WL_CONNECTED);
    if (_lastWifiConnected && !connected) noteWifiDisconnect();
    _lastWifiConnected = connected;

    _data.wifiConnected.value = connected;
    _data.wifiConnected.status = MEAS_LIVE;
    _data.wifiDisconnectCount.value = _wifiDisconnects;
    _data.wifiDisconnectCount.status = MEAS_LIVE;

    if (connected) {
        _data.wifiRssi.value = WiFi.RSSI();
        _data.wifiRssi.status = MEAS_LIVE;
        String ssid = WiFi.SSID();
        strncpy(_data.wifiSsid, ssid.c_str(), sizeof(_data.wifiSsid) - 1);
        _data.wifiSsid[sizeof(_data.wifiSsid) - 1] = '\0';
        _data.wifiSsidStatus = MEAS_LIVE;
    } else {
        _data.wifiRssi.status = MEAS_UNAVAILABLE;
        _data.wifiSsid[0] = '\0';
        _data.wifiSsidStatus = MEAS_UNAVAILABLE;
    }
#else
    (void)force;
#endif
}

void Telemetry::readTemperature() {
#if defined(ESP32)
#if defined(CONFIG_IDF_TARGET_ESP32)
    // The original ESP32 has no usable internal temperature sensor. Arduino's
    // temperatureRead() returns a fixed 53.33 C (128 F) placeholder on that
    // silicon. Reporting that as a measurement would be fabricating data, so it
    // is probed once and then declared unsupported.
    if (!_temperatureProbed) {
        _temperatureProbed = true;
        float probe = temperatureRead();
        _temperatureUsable = !(fabsf(probe - 53.33f) < 0.05f) &&
                             probe > -40.0f && probe < 150.0f;
    }
    if (!_temperatureUsable) {
        _data.temperatureC.status = MEAS_UNSUPPORTED;
        return;
    }
    float t = temperatureRead();
    if (t > -40.0f && t < 150.0f) {
        _data.temperatureC.value = t;
        _data.temperatureC.status = MEAS_LIVE;
    } else {
        _data.temperatureC.status = MEAS_ERROR;
    }
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || \
      defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
      defined(CONFIG_IDF_TARGET_ESP32H2)
    _temperatureProbed = true;
    float t = temperatureRead();
    if (t > -40.0f && t < 150.0f) {
        _data.temperatureC.value = t;
        _data.temperatureC.status = MEAS_LIVE;
        _temperatureUsable = true;
    } else {
        _data.temperatureC.status = MEAS_ERROR;
    }
#else
    _data.temperatureC.status = MEAS_UNSUPPORTED;
#endif
#else
    _data.temperatureC.status = MEAS_UNSUPPORTED;
#endif
}

void Telemetry::readSystem() {
#if defined(ESP32)
    _data.uptimeMs.value = millis();
    _data.uptimeMs.status = MEAS_LIVE;
    _data.resetReason.value = (uint32_t)esp_reset_reason();
    _data.resetReason.status = MEAS_LIVE;
    _data.cpuFreqMhz.value = getCpuFrequencyMhz();
    _data.cpuFreqMhz.status = MEAS_LIVE;
    _data.cycleCount.value = (uint32_t)ESP.getCycleCount();
    _data.cycleCount.status = MEAS_LIVE;
#endif
    _data.errorCount.value = _errorCount;
    _data.errorCount.status = MEAS_LIVE;
    _data.warningCount.value = _warningCount;
    _data.warningCount.status = MEAS_LIVE;
}

void Telemetry::ageCachedFields() {
    // Anything LIVE from the previous pass is, by definition, no longer live.
    if (_data.freeHeap.status == MEAS_LIVE) _data.freeHeap.status = MEAS_CACHED;
    if (_data.cpuFreqMhz.status == MEAS_LIVE) _data.cpuFreqMhz.status = MEAS_CACHED;
    if (_data.uptimeMs.status == MEAS_LIVE) _data.uptimeMs.status = MEAS_CACHED;
}

void Telemetry::updateFast(bool force) {
#if defined(ESP32)
    uint32_t now = millis();
    if (!force && _data.lastFastUpdateMs != 0 &&
        (now - _data.lastFastUpdateMs) < AMELTECH_TELEM_FAST_MIN_INTERVAL_MS) {
        ageCachedFields();
        _lastStatus = MEAS_CACHED;
        return;
    }

    readSystem();
    _data.freeHeap.value = ESP.getFreeHeap();
    _data.freeHeap.status = MEAS_LIVE;
    _data.minFreeHeap.value = ESP.getMinFreeHeap();
    _data.minFreeHeap.status = MEAS_LIVE;
    _data.largestFreeBlock.value = ESP.getMaxAllocHeap();
    _data.largestFreeBlock.status = MEAS_LIVE;
    if (_data.freeHeap.value > 0) {
        uint32_t frag = 0;
        if (_data.largestFreeBlock.value < _data.freeHeap.value) {
            frag = 100u - (uint32_t)((uint64_t)_data.largestFreeBlock.value * 100u /
                                     _data.freeHeap.value);
        }
        _data.heapFragmentationPct.value = frag;
        _data.heapFragmentationPct.status = MEAS_LIVE;
    }
    readWifi(false);

    _data.lastFastUpdateMs = now;
    _lastStatus = MEAS_LIVE;
#else
    (void)force;
    _lastStatus = MEAS_UNSUPPORTED;
#endif
}

void Telemetry::updateFull(bool force) {
#if defined(ESP32)
    uint32_t now = millis();
    if (!force && _data.lastFullUpdateMs != 0 &&
        (now - _data.lastFullUpdateMs) < AMELTECH_TELEM_FULL_MIN_INTERVAL_MS) {
        updateFast(false);
        _lastStatus = MEAS_CACHED;
        return;
    }
    readChipInfo();
    readMemory();
    readWifi(true);
    readTemperature();
    readSystem();
    _data.lastFullUpdateMs = now;
    _data.lastFastUpdateMs = now;
    _lastStatus = MEAS_LIVE;
#else
    (void)force;
    _lastStatus = MEAS_UNSUPPORTED;
#endif
}
