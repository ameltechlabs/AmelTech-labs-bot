#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin();bot.diagnostics().fullScan();bot.ask("How much RAM do I have?");bot.ask("What is my CPU frequency?");bot.ask("Why did my ESP32 restart?");bot.ask("Run diagnostics");}
void loop(){}
