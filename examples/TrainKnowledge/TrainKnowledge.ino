/*
 * TrainKnowledge
 * ---------------------------------------------------------------------------
 * Teaching the bot from code with train() / addQA(), and from the Serial
 * Monitor with the training console.
 *
 * Every lesson is given a four digit data number so it can be deleted again:
 *   train | who made you | AmelTech labs made me.
 *   -> train successfully and save data number code 0001
 *   train | delete | 0001
 *   train | delete | full data
 *
 * Taught entries are stored in NVS and reloaded automatically at begin().
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    Serial.println();
    Serial.print("Taught entries already in flash: ");
    Serial.println((unsigned long)bot.getUserCount());

    // Teaching from code.
    AmelTechError rc = bot.train("who made you", "AmelTech labs made me.");
    Serial.print("train() returned ");
    Serial.print(AmelTechBot::errorToString(rc));
    Serial.print(" - ");
    Serial.println(bot.getLastStatus());

    bot.train("what is my project", "A talking ESP32 that works without internet.");
    bot.train("what is the lab motto", "Measure it, or do not claim it.");

    // Teaching through the console gives the confirmation wording your users see.
    Serial.println(bot.handleSerialLine("train | who is my teacher | Mr Amel teaches us."));

    Serial.println();
    Serial.println("--- asking what was just taught ---");
    Serial.println(bot.ask("who made you"));
    Serial.println(bot.ask("what is my project"));
    Serial.println(bot.ask("who is my teacher"));

    Serial.println();
    Serial.println(bot.handleSerialLine("train | list"));

    // Taught answers take priority over the built-in ones for the same question.
    bot.train("what is wifi", "For this class, Wi-Fi means our lab access point.");
    Serial.println("After overriding a built-in entry:");
    Serial.println(bot.ask("what is wifi"));

    bot.saveKnowledge();

    Serial.println();
    Serial.println("Now try the console yourself:");
    Serial.println(TrainingConsole::helpText());
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
        Serial.println();
    }
    bot.tick();
}
