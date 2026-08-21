/*
 * BasicChat — minimal AmelTech lab's bot example
 * Built-in knowledge is supplied by the library; no train() required.
 */

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);

  bot.begin();

  Serial.println(bot.ask("What is water?"));
  Serial.println(bot.ask("What is ESP32?"));
  Serial.println(bot.ask("How many seconds are in one minute?"));
  Serial.println(bot.ask("hello"));
}

void loop() {
}
