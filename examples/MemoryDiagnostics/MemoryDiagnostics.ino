#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  Serial.println(bot.ask("What is free heap?"));
  const ESP32Telemetry& t = bot.getTelemetry(true);
  Serial.print("Free heap status: ");
  Serial.println(Telemetry::statusToString(t.freeHeap.status));
  if (t.freeHeap.status == MEAS_LIVE) {
    Serial.print("Value: ");
    Serial.println(t.freeHeap.value);
  }
}

void loop() {
}
