// TrainKnowledge.ino
//
// Demonstrates adding custom user knowledge at runtime, handling
// duplicate/contradiction errors, and persisting to NVS.

#include <AmelTechBot.h>

AmelTechBot bot;

void printStatus(const char* label, AmelTechStatus status) {
    Serial.print(label);
    Serial.print(": ");
    Serial.println(ameltechStatusToString(status));
}

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    AmelTechStatus s1 = bot.train(
        "what is my project",
        "My project uses an ESP32 and multiple sensors.",
        "custom"
    );
    printStatus("Train new fact", s1);

    // Attempting the exact same question+answer again -> AMELTECH_DUPLICATE
    AmelTechStatus s2 = bot.train(
        "what is my project",
        "My project uses an ESP32 and multiple sensors.",
        "custom"
    );
    printStatus("Train duplicate", s2);

    // Attempting the same question with a DIFFERENT answer -> AMELTECH_CONTRADICTION
    AmelTechStatus s3 = bot.train(
        "what is my project",
        "My project is a weather station.",
        "custom"
    );
    printStatus("Train contradiction", s3);

    Serial.println(bot.ask("What is my project?"));

    // Persist user knowledge to NVS (ESP32 only)
    AmelTechStatus s4 = bot.saveKnowledge();
    printStatus("Save to NVS", s4);

    Serial.print("Total knowledge entries (built-in + user): ");
    Serial.println(bot.getKnowledgeCount());
}

void loop() {
}
