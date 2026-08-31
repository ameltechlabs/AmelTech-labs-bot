/*
 * BasicChat
 * ---------------------------------------------------------------------------
 * The smallest useful sketch: type a question into the Serial Monitor and the
 * bot answers it. Everything runs on the ESP32, with no network at all.
 *
 * Serial Monitor: 115200 baud, line ending "Newline".
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    delay(300);

    if (!bot.begin()) {
        Serial.println("Bot failed to start. Not enough memory?");
        while (true) delay(1000);
    }

    Serial.println();
    Serial.print("AmelTech lab's bot v");
    Serial.println(AmelTechBot::version());
    Serial.print("Knowledge entries: ");
    Serial.println((unsigned long)bot.getKnowledgeCount());
    Serial.println("Ask me anything. Type 'help' for the command list.");
    Serial.println();
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
        Serial.println();
    }

    // Keeps telemetry, the thermal guard and deferred saves ticking over.
    bot.tick();
}
