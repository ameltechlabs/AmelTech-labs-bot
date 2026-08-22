// FullDemo.ino
//
// End-to-end tour of AmelTechBot: built-in knowledge, custom
// training, calculator, context memory, telemetry, diagnostics,
// health scoring, trolling mode, and error handling.

#include <AmelTechBot.h>

AmelTechBot bot;

void section(const char* title) {
    Serial.println();
    Serial.println("===== " + String(title) + " =====");
}

void setup() {
    Serial.begin(115200);
    delay(500);

    AmelTechStatus beginStatus = bot.begin();
    Serial.print("begin() status: ");
    Serial.println(ameltechStatusToString(beginStatus));

    section("Built-in knowledge");
    Serial.println(bot.ask("What is water?"));
    Serial.println(bot.ask("What is gravity?"));

    section("Custom training");
    AmelTechStatus trainStatus = bot.train("what is my project", "An ESP32-based sensor hub.", "custom");
    Serial.println(ameltechStatusToString(trainStatus));
    Serial.println(bot.ask("What is my project?"));

    section("Calculator");
    Serial.println(bot.ask("(25 + 5) * 2"));
    CalcResult calc = bot.calculate("10 / 0");
    Serial.print("10 / 0 -> valid=");
    Serial.print(calc.valid);
    Serial.print(" message=");
    Serial.println(calc.message);

    section("Context memory");
    Serial.println(bot.ask("What is my Wi-Fi RSSI?"));
    Serial.println(bot.ask("Is that good?"));

    section("Telemetry & Diagnostics");
    DiagnosticsReport report = bot.runDiagnostics(true);
    Serial.println(report.summary);

    section("Health report");
    HealthReport health = bot.getHealthReport();
    Serial.print("Overall: ");
    Serial.print(health.overallScore);
    Serial.print("/100 (");
    Serial.print(healthLevelToString(health.overallLevel));
    Serial.println(")");

    section("Trolling mode");
    bot.enableTrolling(true);
    Serial.println(bot.ask("What is my free heap?"));
    bot.enableTrolling(false);

    section("Low-confidence handling");
    Serial.println(bot.ask("What is the airspeed velocity of an unladen swallow?"));
    Serial.print("Confidence: ");
    Serial.println(bot.getConfidence());
    Serial.print("Status: ");
    Serial.println(ameltechStatusToString(bot.getLastStatus()));

    section("Knowledge count");
    Serial.print("Total entries: ");
    Serial.println(bot.getKnowledgeCount());
}

void loop() {
}
