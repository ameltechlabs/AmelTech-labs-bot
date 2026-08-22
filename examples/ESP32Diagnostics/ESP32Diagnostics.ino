// ESP32Diagnostics.ino
//
// Demonstrates a full diagnostics scan (slow telemetry included)
// and the explainable health report.

#include <AmelTechBot.h>

AmelTechBot bot;

void printMeasurement(const char* label, AmelTechMeasurementStatus status) {
    Serial.print(label);
    Serial.print(": ");
    Serial.println(measurementStatusToString(status));
}

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    Serial.println("Running full diagnostics scan...");
    DiagnosticsReport report = bot.runDiagnostics(true); // full scan

    Serial.println(report.summary);
    Serial.println();

    Serial.print("Chip family: ");
    Serial.println(esp32FamilyToString(report.telemetry.chip.family));

    Serial.print("CPU frequency status: ");
    Serial.println(measurementStatusToString(report.telemetry.cpu.frequencyMHz.status));
    if (report.telemetry.cpu.frequencyMHz.status == MEAS_LIVE) {
        Serial.print("CPU frequency: ");
        Serial.print(report.telemetry.cpu.frequencyMHz.value);
        Serial.println(" MHz");
    }

    Serial.print("Free heap status: ");
    Serial.println(measurementStatusToString(report.telemetry.memory.freeHeapBytes.status));
    if (report.telemetry.memory.freeHeapBytes.status == MEAS_LIVE) {
        Serial.print("Free heap: ");
        Serial.print(report.telemetry.memory.freeHeapBytes.value);
        Serial.println(" bytes");
    }

    Serial.println();
    Serial.println("Subsystem health:");
    for (uint8_t i = 0; i < report.health.subsystemCount; i++) {
        SubsystemHealth& sh = report.health.subsystems[i];
        Serial.print("  ");
        Serial.print(sh.name);
        Serial.print(": ");
        Serial.print(sh.wasMeasured ? healthLevelToString(sh.level) : "UNKNOWN (not measured)");
        Serial.print(" - ");
        Serial.println(sh.note);
    }
}

void loop() {
}
