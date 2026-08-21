#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  const char* questions[] = {
    "what is gravity",
    "what is wifi",
    "how many bits in a byte",
    "what is ohm's law",
    "who are you",
    "what is the weather"  // expected low confidence / offline
  };

  for (size_t i = 0; i < sizeof(questions) / sizeof(questions[0]); ++i) {
    Serial.print("> ");
    Serial.println(questions[i]);
    Serial.println(bot.ask(questions[i]));
    Serial.print("  confidence=");
    Serial.println(bot.getConfidence(), 2);
    Serial.println();
  }
}

void loop() {
}
