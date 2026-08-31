/*
 * ESP32Diagnostics
 * ---------------------------------------------------------------------------
 * A full diagnostic sweep every ten seconds.
 *
 * The important idea here is honesty. Every reading carries a status, and a
 * value that could not be measured is reported as UNSUPPORTED or UNAVAILABLE
 * rather than being replaced by a plausible looking number. On the classic
 * ESP32 the internal temperature sensor returns a fixed 53.33 C placeholder;
 * this library detects that and refuses to present it as a real measurement.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;
unsigned long lastRun = 0;

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();
    Serial.println("ESP32 diagnostics starting...");
}

// MeasU and MeasI differ only in the value type, so one template covers both.
template <typename T>
void printMeasurement(const char* label, const T& m, const char* unit) {
    Serial.print(label);
    if (Telemetry::statusIsUsable(m.status)) {
        Serial.print(m.value);
        Serial.print(' ');
        Serial.print(unit);
        if (m.status == MEAS_CACHED) Serial.print("  (cached)");
    } else {
        Serial.print(Telemetry::statusToString(m.status));
    }
    Serial.println();
}

void loop() {
    if (millis() - lastRun >= 10000) {
        lastRun = millis();

        Serial.println();
        Serial.println(bot.runDiagnostics(true));

        // The same data is also available field by field.
        const ESP32Telemetry& t = bot.getTelemetry(true);
        Serial.println("--- selected fields ---");
        printMeasurement("Free heap      : ", t.freeHeap, "bytes");
        printMeasurement("Min free heap  : ", t.minFreeHeap, "bytes");
        printMeasurement("CPU frequency  : ", t.cpuFreqMhz, "MHz");
        printMeasurement("Uptime         : ", t.uptimeMs, "ms");
        printMeasurement("Wi-Fi RSSI     : ", t.wifiRssi, "dBm");

        Serial.print("Die temperature: ");
        if (Telemetry::statusIsUsable(t.temperatureC.status)) {
            Serial.print(t.temperatureC.value, 1);
            Serial.println(" C");
        } else {
            Serial.println(Telemetry::statusToString(t.temperatureC.status));
        }

        Serial.print("Health score   : ");
        Serial.print(bot.getHealthScore());
        Serial.println("/100");
    }

    bot.tick();
}
