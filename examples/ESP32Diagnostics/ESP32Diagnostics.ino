#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  Serial.println(bot.runDiagnostics(true));
  Serial.println();
  Serial.println(bot.getHealthReport());
}

void loop() {
}
