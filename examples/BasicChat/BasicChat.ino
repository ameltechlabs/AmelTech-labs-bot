// BasicChat.ino
//
// Minimal example: ask a few built-in questions. No training
// required — built-in knowledge ships inside the library itself.

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    Serial.println(bot.ask("What is water?"));
    Serial.println(bot.ask("What is ESP32?"));
    Serial.println(bot.ask("How many seconds are there in one minute?"));
    Serial.println(bot.ask("how many sec r there in 1 min")); // fuzzy/abbreviation match
    Serial.println(bot.ask("What is quantum entanglement?"));  // not in knowledge base -> low confidence
}

void loop() {
}
