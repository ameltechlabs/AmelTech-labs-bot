#include "Telemetry.h"
#include <string.h>

#if defined(ESP32)
#include <esp_system.h>
#include <esp_chip_info.h>
#include <WiFi.h>
#if defined(ESP_IDF_VERSION)
#include <esp_idf_version.h>
#endif
#endif

Telemetry::Telemetry() : _lastStatus(MEAS_UNAVAILABLE), _begun(false) {
    memset(&_data, 0, sizeof(_data));
    clearUnsupported();
}

void Telemetry::clearUnsupported() {
    auto setU = [](MeasU& m) { m.value = 0; m.status = MEAS_UNSUPPORTED; };
    auto setI = [](MeasI& m) { m.value = 0; m.status = MEAS_UNSUPPORTED; };
    auto setF = [](MeasF& m) { m.value = 0; m.status = MEAS_UNSUPPORTED; };
    auto setB = [](MeasB& m) { m.value = false; m.status = MEAS_UNSUPPORTED; };

    _data.chipModel[0] = '\0';
    _data.chipModelStatus = MEAS_UNSUPPORTED;
    _data.chipCores = 0;
    _data.chipCoresStatus = MEAS_UNSUPPORTED;
    _data.chipRevision = 0;
    _data.chipRevisionStatus = MEAS_UNSUPPORTED;
    _data.mac = 0;
    _data.macStatus = MEAS_UNSUPPORTED;

    setU(_data.cpuFreqMhz);
    setU(_data.cycleCount);
    setU(_data.freeHeap);
    setU(_data.minFreeHeap);
    setU(_data.heapSize);
    setU(_data.largestFreeBlock);
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
    // Throughput remains UNAVAILABLE unless measured
    _data.wifiTxThroughputBps.status = MEAS_UNAVAILABLE;
    _data.wifiRxThroughputBps.status = MEAS_UNAVAILABLE;

    setB(_data.btEnabled);
    setU(_data.uptimeMs);
    setU(_data.resetReason);
    setF(_data.temperatureC);
    setU(_data.errorCount);
    _data.lastUpdateMs = 0;
}

const char* Telemetry::statusToString(MeasurementStatus s) {
    switch (s) {
        case MEAS_LIVE: return "LIVE";
        case MEAS_CACHED: return "CACHED";
        case MEAS_STALE: return "STALE";
        case MEAS_UNAVAILABLE: return "UNAVAILABLE";
        case MEAS_UNSUPPORTED: return "UNSUPPORTED";
        case MEAS_ERROR: return "MEASUREMENT_ERROR";
        default: return "UNKNOWN";
    }
}

const char* Telemetry::resetReasonToString(uint32_t reason) {
#if defined(ESP32)
    switch ((esp_reset_reason_t)reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXTERNAL";
        case ESP_RST_SW: return "SOFTWARE";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INTERRUPT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
#else
    (void)reason;
    return "UNSUPPORTED";
#endif
}

void Telemetry::begin() {
    clearUnsupported();
#if defined(ESP32)
    readChipInfo();
    readSystem();
    updateFast();
#endif
    _begun = true;
    _lastStatus = MEAS_LIVE;
}

void Telemetry::readChipInfo() {
#if defined(ESP32)
    esp_chip_info_t info;
    esp_chip_info(&info);
    _data.chipCores = info.cores;
    _data.chipCoresStatus = MEAS_LIVE;
    _data.chipRevision = info.revision;
    _data.chipRevisionStatus = MEAS_LIVE;

    const char* model = "ESP32";
#if defined(CONFIG_IDF_TARGET_ESP32)
    model = "ESP32";
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    model = "ESP32-S2";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    model = "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    model = "ESP32-C3";
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
    model = "ESP32-C6";
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
    model = "ESP32-H2";
#else
    // Fallback to Arduino helper if available
#if defined(ESP_getChipModel) || 1
    // ESP.getChipModel() available in recent cores
    String m = ESP.getChipModel();
    if (m.length() > 0 && m.length() < sizeof(_data.chipModel)) {
        strncpy(_data.chipModel, m.c_str(), sizeof(_data.chipModel) - 1);
        _data.chipModel[sizeof(_data.chipModel) - 1] = '\0';
        _data.chipModelStatus = MEAS_LIVE;
        model = nullptr;
    }
#endif
#endif
    if (model) {
        strncpy(_data.chipModel, model, sizeof(_data.chipModel) - 1);
        _data.chipModel[sizeof(_data.chipModel) - 1] = '\0';
        _data.chipModelStatus = MEAS_LIVE;
    }

    _data.mac = ESP.getEfuseMac();
    _data.macStatus = MEAS_LIVE;
#else
    (void)0;
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

#if defined(ESP_getMaxAllocHeap) || 1
    _data.largestFreeBlock.value = ESP.getMaxAllocHeap();
    _data.largestFreeBlock.status = MEAS_LIVE;
#endif

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
#endif
}

void Telemetry::readWifi() {
#if defined(ESP32)
    wl_status_t st = WiFi.status();
    _data.wifiStatus.value = (uint32_t)st;
    _data.wifiStatus.status = MEAS_LIVE;
    bool connected = (st == WL_CONNECTED);
    _data.wifiConnected.value = connected;
    _data.wifiConnected.status = MEAS_LIVE;

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
    // Throughput not measured by default
    _data.wifiTxThroughputBps.status = MEAS_UNAVAILABLE;
    _data.wifiRxThroughputBps.status = MEAS_UNAVAILABLE;
#endif
}

void Telemetry::readTemperature() {
#if defined(ESP32)
    // Internal temperature sensor support varies widely.
    // Only report when a reliable API is clearly available.
    // Many cores expose temperatureRead() for classic ESP32; accuracy is limited.
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
    // temperatureRead() returns Fahrenheit on some older cores, Celsius on others.
    // To avoid fabricating accuracy claims we mark as LIVE only when API exists,
    // and document limited accuracy in docs.
    float t = temperatureRead();
    // Heuristic: if value is in a plausible chip range
    if (t > -40.0f && t < 150.0f) {
        _data.temperatureC.value = t;
        _data.temperatureC.status = MEAS_LIVE;
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
#endif
}

void Telemetry::updateFast() {
#if defined(ESP32)
    readSystem();
    _data.freeHeap.value = ESP.getFreeHeap();
    _data.freeHeap.status = MEAS_LIVE;
    _data.minFreeHeap.value = ESP.getMinFreeHeap();
    _data.minFreeHeap.status = MEAS_LIVE;
    readWifi();
    _data.lastUpdateMs = millis();
    _lastStatus = MEAS_LIVE;
#else
    _lastStatus = MEAS_UNSUPPORTED;
#endif
}

void Telemetry::updateFull() {
#if defined(ESP32)
    readChipInfo();
    readMemory();
    readWifi();
    readTemperature();
    readSystem();
    _data.lastUpdateMs = millis();
    _lastStatus = MEAS_LIVE;
#else
    _lastStatus = MEAS_UNSUPPORTED;
#endif
}
