// MemoryDiagnostics.ino
//
// Demonstrates memory telemetry: free heap (fast) plus flash size,
// PSRAM, and fragmentation (slow / full-scan only).

#include <AmelTechBot.h>

AmelTechBot bot;

void printMem(const char* label, uint32_t value, AmelTechMeasurementStatus status, const char* unit) {
    Serial.print(label);
    Serial.print(": ");
    if (status == MEAS_LIVE) {
        Serial.print(value);
        Serial.println(unit);
    } else {
        Serial.println(measurementStatusToString(status));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    Serial.println(bot.ask("What is my free RAM?"));

    // Fast telemetry only includes free heap; flash/PSRAM require full scan
    ESP32Telemetry fast = bot.getTelemetry(false);
    printMem("Free heap (fast)", fast.memory.freeHeapBytes.value, fast.memory.freeHeapBytes.status, " bytes");
    printMem("Flash size (fast, expect UNAVAILABLE)", fast.memory.flashSizeBytes.value, fast.memory.flashSizeBytes.status, " bytes");

    ESP32Telemetry full = bot.getTelemetry(true);
    printMem("Free heap (full)", full.memory.freeHeapBytes.value, full.memory.freeHeapBytes.status, " bytes");
    printMem("Largest free block", full.memory.largestFreeBlockBytes.value, full.memory.largestFreeBlockBytes.status, " bytes");
    printMem("Flash size", full.memory.flashSizeBytes.value, full.memory.flashSizeBytes.status, " bytes");
    printMem("Firmware size", full.memory.firmwareSizeBytes.value, full.memory.firmwareSizeBytes.status, " bytes");
    printMem("PSRAM total", full.memory.psramTotalBytes.value, full.memory.psramTotalBytes.status, " bytes");

    Serial.print("Heap fragmentation: ");
    if (full.memory.heapFragmentationPct.status == MEAS_LIVE) {
        Serial.print(full.memory.heapFragmentationPct.value);
        Serial.println(" %");
    } else {
        Serial.println(measurementStatusToString(full.memory.heapFragmentationPct.status));
    }
}

void loop() {
}
