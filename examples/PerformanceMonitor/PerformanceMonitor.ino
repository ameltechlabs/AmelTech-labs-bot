/*
 * PerformanceMonitor
 * ---------------------------------------------------------------------------
 * Measures how long the matcher actually takes, and shows the thermal guard
 * and the query cache doing their jobs.
 *
 * Version 2 replaced a full scan of every row with a two stage search: a cheap
 * signature and bloom prefilter over all entries, then full scoring on a small
 * candidate pool. On an ESP32 at 240 MHz a typical query takes tens of
 * microseconds rather than several milliseconds, which is what keeps the chip
 * cool and the watchdog quiet.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

const char* PROBES[] = {
    "what is wifi",
    "what is the speed of light",
    "how does a transistor work",
    "what is teh speed of light",
    "tell me about bluetooth",
    "completely unknown gibberish phrase"
};

void benchmark() {
    Serial.println();
    Serial.println("query                                 us   conf   candidates  fallback");
    Serial.println("---------------------------------------------------------------------");

    for (unsigned i = 0; i < sizeof(PROBES) / sizeof(PROBES[0]); ++i) {
        unsigned long t0 = micros();
        bot.ask(PROBES[i]);
        unsigned long elapsed = micros() - t0;

        char line[110];
        snprintf(line, sizeof(line), "%-36s %6lu  %4.2f   %9u  %s",
                 PROBES[i], elapsed, (double)bot.getConfidence(),
                 (unsigned)bot.knowledge().lastCandidateCount(),
                 bot.knowledge().lastUsedFallbackPass() ? "yes" : "no");
        Serial.println(line);
    }

    Serial.println();
    Serial.print("Matcher scan time of the last query: ");
    Serial.print(bot.getLastScanMicros());
    Serial.println(" us");
    Serial.print("Total queries: ");
    Serial.print((unsigned long)bot.knowledge().queryCount());
    Serial.print(", served from cache: ");
    Serial.println((unsigned long)bot.knowledge().cacheHits());
}

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    benchmark();

    // Repeating a question hits the single slot cache and costs almost nothing.
    Serial.println();
    Serial.println("Repeating the same question ten times:");
    unsigned long t0 = micros();
    for (int i = 0; i < 10; ++i) bot.ask("what is wifi");
    Serial.print("10 repeats took ");
    Serial.print(micros() - t0);
    Serial.println(" us in total");
    Serial.print("cache hits now: ");
    Serial.println((unsigned long)bot.knowledge().cacheHits());
}

void loop() {
    static unsigned long last = 0;
    if (millis() - last >= 20000) {
        last = millis();
        Serial.println();
        Serial.println(bot.getThermalReport());
        Serial.print("Loop duty: ");
        Serial.print(bot.thermal().dutyPercent());
        Serial.println("%");
    }
    bot.tick();
}
