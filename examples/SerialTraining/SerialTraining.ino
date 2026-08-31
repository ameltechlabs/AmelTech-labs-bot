/*
 * SerialTraining
 * ---------------------------------------------------------------------------
 * The serial training console, on its own.
 *
 * Open the Serial Monitor at 115200 with the line ending set to "Newline",
 * then try:
 *
 *   train | who made you | AmelTech labs made me.
 *      -> train successfully and save data number code 0001
 *
 *   train | list                 show everything taught, with its code
 *   train | status               memory, capacity and whether training is open
 *   train | delete | 0001        delete one entry by its data number
 *   train | delete | full data   delete every taught entry
 *   train | save                 write to flash now
 *   train | help                 the command reminder
 *
 * Training is refused while free heap is at or below the reserved minimum,
 * which is 200 KB by default. That reserve keeps chat logging and the matcher
 * working, so filling the training memory can never take the chatbot down.
 * The console explains this in plain language rather than failing quietly.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    // The reserve is adjustable. Lower it only if you know what you are doing.
    // bot.training().setMinFreeHeap(150UL * 1024UL);

    Serial.println();
    Serial.println("=== AmelTech training console ===");
    Serial.println(TrainingConsole::helpText());
    Serial.println();
    Serial.println(bot.training().statusReport());
    Serial.println();

    // A worked example, run from code so you can see the exact wording.
    Serial.println(bot.handleSerialLine("train | what is our lab name | AmelTech labs."));
    Serial.println(bot.handleSerialLine("train | list"));
    Serial.println(bot.ask("what is our lab name"));
    Serial.println();
    Serial.println("Your turn.");
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
        Serial.println();
    }
    bot.tick();
}
