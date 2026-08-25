#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(800);
  bot.begin();

  Serial.println("=== AmelTech lab's bot Full Demo ===");
  Serial.print("Builtin knowledge entries: ");
  Serial.println(bot.getBuiltinCount());

  Serial.println("\n-- Knowledge --");
  Serial.println(bot.ask("What is water?"));
  Serial.println(bot.ask("how many sec r there in 1 min"));  // fuzzy / abbrev
  Serial.println(bot.ask("What is ESP32?"));

  Serial.println("\n-- Calculator --");
  Serial.println(bot.ask("(10 + 5) * 3"));
  Serial.println(bot.calculate("100 / 4"));

  Serial.println("\n-- Training --");
  bot.train("what is my board", "This is a custom ESP32 development board.", "custom");
  Serial.println(bot.ask("what is my board"));

  Serial.println("\n-- Hardware --");
  Serial.println(bot.ask("What is free heap?"));
  Serial.println(bot.ask("What is CPU frequency?"));
  Serial.println(bot.ask("What is uptime?"));

  Serial.println("\n-- Diagnostics --");
  Serial.println(bot.runDiagnostics(false));

  Serial.println("\n-- Health --");
  Serial.println(bot.getHealthReport());

  Serial.println("\n-- Trolling --");
  bot.enableTrolling(true);
  Serial.println(bot.ask("What is WiFi?"));

  Serial.println("\nDone.");
}

void loop() {
}
