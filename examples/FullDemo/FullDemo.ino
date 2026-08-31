/*
 * FullDemo
 * ---------------------------------------------------------------------------
 * Everything the library does, in one sketch.
 *
 *   - general knowledge with confidence reporting
 *   - maths written the way people type it
 *   - DHT11 / DHT22 sensing with situation analysis (optional)
 *   - name memory that survives a power cycle
 *   - serial training console
 *   - honest telemetry, thermal guard and weighted health scoring
 *
 * Open the Serial Monitor at 115200 with the line ending set to "Newline".
 * Type "help" once it is running.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

// Set to a real GPIO to enable the sensor half of the demo.
#define USE_DHT      0
#define DHT_PIN      4
#define DHT_KIND     DHT_TYPE_22

AmelTechBot bot;
unsigned long lastHeartbeat = 0;

void banner() {
    Serial.println();
    Serial.println("===========================================");
    Serial.print  ("  AmelTech lab's bot  v");
    Serial.println(AmelTechBot::version());
    Serial.println("  Offline. On-chip. No cloud, no API key.");
    Serial.println("===========================================");
    Serial.print("Knowledge : ");
    Serial.print((unsigned long)bot.getBuiltinCount());
    Serial.print(" built in + ");
    Serial.print((unsigned long)bot.getUserCount());
    Serial.println(" taught");
    Serial.print("Names      : ");
    Serial.print((unsigned long)bot.getUserProfileCount());
    Serial.print(" of ");
    Serial.println(UserProfileStore::capacity());
    Serial.print("Sensor     : ");
    Serial.println(bot.hasSensor() ? bot.sensors().typeName() : "none");
    Serial.println();
}

void demo(const char* label, const char* question) {
    Serial.print(label);
    Serial.println(question);
    Serial.print("  -> ");
    Serial.println(bot.ask(question));
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(400);

    if (!bot.begin()) {
        Serial.println("begin() failed.");
        while (true) delay(1000);
    }

#if USE_DHT
    bot.beginDHT(DHT_PIN, DHT_KIND);
    delay(2000);          // the first DHT reading needs a moment
#endif

    banner();

    Serial.println("--- knowledge ---");
    demo("Q: ", "what is wifi");
    demo("Q: ", "tell me about bluetooth");
    demo("Q: ", "what is teh speed of light");

    Serial.println("--- maths ---");
    demo("Q: ", "what is 25 * 4");
    demo("Q: ", "15% of 200");
    demo("Q: ", "sqrt(144) + 7!");

    Serial.println("--- hardware ---");
    demo("Q: ", "how much free memory do you have");
    demo("Q: ", "what is your health score");

#if USE_DHT
    Serial.println("--- room conditions ---");
    demo("Q: ", "how is the room");
#endif

    Serial.println("--- training ---");
    Serial.println(bot.handleSerialLine("train | who made you | AmelTech labs made me."));
    demo("Q: ", "who made you");

    Serial.println("--- identity ---");
    demo("Q: ", "hi my name is Joky Pk");
    demo("Q: ", "what is my name");

    Serial.println("--- diagnostics ---");
    Serial.println(bot.runDiagnostics(true));

    Serial.println();
    Serial.println("Ready. Type a question, or 'help'.");
    Serial.println("Reset the board to see the name memory in action.");
    Serial.println();
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
        Serial.println();
    }

    if (millis() - lastHeartbeat >= 60000) {
        lastHeartbeat = millis();
        Serial.print("[heartbeat] health ");
        Serial.print(bot.getHealthScore());
        Serial.print("/100, ");
        Serial.println(bot.getThermalReport());
    }

    bot.tick();
}
