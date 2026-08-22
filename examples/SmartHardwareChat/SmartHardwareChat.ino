// SmartHardwareChat.ino
//
// Demonstrates natural-language hardware questions answered from
// real telemetry, plus optional trolling mode commentary.

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();
    bot.enableTrolling(true); // optional, harmless commentary appended after real answers

    Serial.println(bot.ask("What is my free heap?"));
    Serial.println(bot.ask("What is the CPU frequency?"));
    Serial.println(bot.ask("How long has this device been running?"));
    Serial.println(bot.ask("What is the ESP32 health?"));

    // Mixed: general knowledge still works in the same conversation
    Serial.println(bot.ask("What is ESP32?"));
}

void loop() {
}
