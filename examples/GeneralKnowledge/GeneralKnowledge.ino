/*
 * GeneralKnowledge
 * ---------------------------------------------------------------------------
 * Shows how the matcher generalises over wording. The same fact is asked in
 * several ways, including with typos, and the confidence for each attempt is
 * printed so you can see how the scoring behaves.
 *
 * Nothing here is generated text: every answer is a stored fact. When the bot
 * is not confident it says so rather than inventing something.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

const char* QUESTIONS[] = {
    "what is wifi",
    "What is Wi-Fi?",
    "whats wifi",
    "tell me about bluetooth",
    "explain i2c",
    "what is teh speed of light",     // typo, still matches
    "waht is gravity",                // typo, still matches
    "what is ohm's law",
    "define capacitor",
    "who was albert einstein",
    "flibbertigibbet protocol"        // genuinely unknown
};

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    Serial.println();
    Serial.print("Built-in entries: ");
    Serial.println((unsigned long)bot.getBuiltinCount());
    Serial.println();

    for (unsigned i = 0; i < sizeof(QUESTIONS) / sizeof(QUESTIONS[0]); ++i) {
        Serial.print("Q: ");
        Serial.println(QUESTIONS[i]);

        String answer = bot.ask(QUESTIONS[i]);

        Serial.print("A: ");
        Serial.println(answer);
        Serial.print("   confidence ");
        Serial.print(bot.getConfidence() * 100.0f, 1);
        Serial.print("%, scan took ");
        Serial.print(bot.getLastScanMicros());
        Serial.println(" us");
        Serial.println();
    }
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
    }
    bot.tick();
}
