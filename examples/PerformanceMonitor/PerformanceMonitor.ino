#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  uint32_t t0 = millis();
  for (int i = 0; i < 20; ++i) {
    bot.ask("what is water");
  }
  uint32_t elapsed = millis() - t0;
  Serial.print("20 knowledge queries took ");
  Serial.print(elapsed);
  Serial.println(" ms");

  Serial.println(bot.ask("What is CPU frequency?"));
  Serial.println(bot.getHealthReport());
}

void loop() {
}
