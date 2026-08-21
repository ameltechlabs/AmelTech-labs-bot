#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  AmelTechError e = bot.train(
    "what is my project",
    "My project uses an ESP32 and multiple sensors.",
    "custom"
  );
  Serial.print("Train status: ");
  Serial.println(bot.getLastStatus());

  // Duplicate should be rejected
  e = bot.train(
    "what is my project",
    "Something different that conflicts.",
    "custom"
  );
  Serial.print("Conflict/duplicate status: ");
  Serial.println(bot.getLastStatus());

  Serial.println(bot.ask("what is my project"));

  bot.saveKnowledge();  // persist user knowledge to NVS
}

void loop() {
}
