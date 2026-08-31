/*
 * MemoryDiagnostics
 * ---------------------------------------------------------------------------
 * Watches heap usage while the bot works, and shows what the training heap
 * guard is protecting.
 *
 * Training is refused while free heap is at or below the reserved minimum
 * (200 KB by default). That reserve is what keeps chat logging and the matcher
 * alive, so a full training memory can never take the chatbot down.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;
unsigned long lastReport = 0;

void reportMemory(const char* when) {
    const ESP32Telemetry& t = bot.getTelemetry(true);

    Serial.print(when);
    Serial.print("  free ");
    Serial.print(Telemetry::statusIsUsable(t.freeHeap.status)
                     ? (long)(t.freeHeap.value / 1024) : -1L);
    Serial.print(" KB, low water ");
    Serial.print(Telemetry::statusIsUsable(t.minFreeHeap.status)
                     ? (long)(t.minFreeHeap.value / 1024) : -1L);
    Serial.print(" KB, fragmentation ");
    Serial.print(Telemetry::statusIsUsable(t.heapFragmentationPct.status)
                     ? (long)t.heapFragmentationPct.value : -1L);
    Serial.print("%, taught entries use ");
    Serial.print((unsigned long)(bot.knowledge().userHeapBytes() / 1024));
    Serial.println(" KB");
}

void setup() {
    Serial.begin(115200);
    delay(300);

    reportMemory("before begin() :");
    bot.begin();
    reportMemory("after begin()  :");

    // Built-in knowledge lives in flash, so asking questions costs almost no RAM.
    for (int i = 0; i < 20; ++i) bot.ask("what is wifi");
    reportMemory("after 20 asks  :");

    // Teach a few entries; each one is a fixed-size heap block.
    for (int i = 0; i < 5; ++i) {
        String q = "test question ";
        q += i;
        bot.train(q, "This is a test answer stored in RAM and NVS.");
    }
    reportMemory("after 5 lessons:");

    Serial.println();
    Serial.println(bot.training().statusReport());
    Serial.println();
    Serial.println(bot.getHealthReport());
}

void loop() {
    if (millis() - lastReport >= 15000) {
        lastReport = millis();
        reportMemory("periodic       :");
    }
    bot.tick();
}
