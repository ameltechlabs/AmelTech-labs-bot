/*
 * SmartHardwareChat
 * ---------------------------------------------------------------------------
 * Ask the board about itself in plain English.
 *
 * The bot recognises hardware questions and answers them from live telemetry
 * instead of from stored text. Where a value cannot be measured it says so.
 *
 * Try:
 *   how much free memory do you have
 *   what is your cpu speed
 *   how hot is the chip
 *   what chip is this
 *   how long have you been running
 *   wifi status
 *   what is your health score
 *   run diagnostics
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

const char* DEMO[] = {
    "how much free memory do you have",
    "what is your cpu speed",
    "how hot is the chip",
    "what chip is this",
    "how long have you been running",
    "wifi status",
    "what is your health score"
};

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    // Optional: with a DHT attached the bot can also talk about the room.
    // bot.beginDHT(4, DHT_TYPE_22);

    Serial.println();
    Serial.println("=== Hardware questions ===");
    for (unsigned i = 0; i < sizeof(DEMO) / sizeof(DEMO[0]); ++i) {
        Serial.print("Q: ");
        Serial.println(DEMO[i]);
        Serial.print("A: ");
        Serial.println(bot.ask(DEMO[i]));

        // Hardware answers also report how the value was obtained.
        Serial.print("   measurement status: ");
        Serial.println(Telemetry::statusToString(bot.getMeasurementStatus()));
        Serial.println();
    }

    Serial.println("Ask your own hardware question:");
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
        Serial.println();
    }
    bot.tick();
}
