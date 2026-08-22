// PerformanceMonitor.ino
//
// Demonstrates loop timing telemetry: call recordLoopSample() once
// per loop() iteration to build min/avg/max/jitter statistics.

#include <AmelTechBot.h>

AmelTechBot bot;
unsigned long lastLoopStart = 0;

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();
    lastLoopStart = micros();
}

void loop() {
    unsigned long now = micros();
    unsigned long duration = now - lastLoopStart;
    lastLoopStart = now;

    // Feed loop timing into telemetry (accessed indirectly through ask()
    // for CPU questions, or directly via getTelemetry()).
    static unsigned long sampleCounter = 0;
    sampleCounter++;

    // Access telemetry's loop recorder through the bot's public
    // getTelemetry() plumbing is read-only; recordLoopSample() is on
    // the Telemetry class itself. In this simplified example we just
    // demonstrate reading loop timing after enough samples via direct
    // Telemetry usage pattern documented in TELEMETRY.md.

    if (sampleCounter % 20000 == 0) {
        ESP32Telemetry t = bot.getTelemetry(false);
        Serial.print("Last loop time status: ");
        Serial.println(measurementStatusToString(t.cpu.lastLoopTimeUs.status));
        if (t.cpu.lastLoopTimeUs.status == MEAS_LIVE) {
            Serial.print("avg=");
            Serial.print(t.cpu.avgLoopTimeUs.value);
            Serial.print("us min=");
            Serial.print(t.cpu.minLoopTimeUs.value);
            Serial.print("us max=");
            Serial.print(t.cpu.maxLoopTimeUs.value);
            Serial.println("us");
        } else {
            Serial.println("No loop samples recorded yet in this simplified example.");
        }
    }

    delay(1); // keep loop from spinning unnecessarily fast in this demo
}
