#include <AmelTechBot.h>
#include <WiFi.h>

// Optional: set credentials to see LIVE RSSI
// const char* ssid = "your-ssid";
// const char* password = "your-password";

AmelTechBot bot;

void setup() {
  Serial.begin(115200);
  delay(500);
  bot.begin();

  // WiFi.begin(ssid, password);
  // delay(3000);

  Serial.println(bot.ask("What is my Wi-Fi RSSI?"));
  Serial.println(bot.ask("Is Wi-Fi connected?"));
  Serial.println(bot.ask("Is that good?"));  // context follow-up if RSSI was answered
}

void loop() {
}
