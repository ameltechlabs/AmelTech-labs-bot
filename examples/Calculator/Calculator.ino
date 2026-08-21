#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  Serial.println(bot.calculate("25 * 4"));
  Serial.println(bot.calculate("100 / 5"));
  Serial.println(bot.calculate("(25 + 5) * 2"));
  Serial.println(bot.calculate("50%"));
  Serial.println(bot.calculate("25 + 10%"));  // 25 + 0.1 = 25.1

  // Via ask() intent detection
  Serial.println(bot.ask("25 * 4"));

  // Division by zero
  String bad = bot.calculate("10 / 0");
  Serial.print("Div0 result empty? ");
  Serial.println(bad.length() == 0 ? "yes" : "no");
  Serial.println(bot.getLastStatus());
}

void loop() {
}
