// GeneralKnowledge.ino
//
// Demonstrates the built-in general/science/math knowledge base
// and confidence introspection.

#include <AmelTechBot.h>

AmelTechBot bot;

void askAndReport(const String& q) {
    String answer = bot.ask(q);
    Serial.println("Q: " + q);
    Serial.println("A: " + answer);
    Serial.print("Confidence: ");
    Serial.println(bot.getConfidence());
    Serial.print("Status: ");
    Serial.println(ameltechStatusToString(bot.getLastStatus()));
    Serial.println("---");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    askAndReport("What is gravity?");
    askAndReport("What is the capital of France?");
    askAndReport("What is photosynthesis?");
    askAndReport("Who created Arduino?");
    askAndReport("What is the speed of light?");
    askAndReport("What is the meaning of life?"); // out of scope -> low confidence
}

void loop() {
}
