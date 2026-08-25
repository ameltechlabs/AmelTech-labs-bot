#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();
  bot.enableTrolling(true);

  Serial.println(bot.ask("What is ESP32?"));
  Serial.println(bot.ask("What is free heap?"));
  Serial.println(bot.ask("What is my Wi-Fi RSSI?"));
  Serial.println(bot.ask("Is that good?"));
  Serial.println(bot.ask("Run diagnostics"));
}

void loop() {
  // Optional interactive mode:
  // if (Serial.available()) {
  //   String q = Serial.readStringUntil('\n');
  //   q.trim();
  //   if (q.length()) Serial.println(bot.ask(q));
  // }
}
