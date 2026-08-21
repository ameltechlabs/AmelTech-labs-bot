#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin();bot.ask("How much RAM do I have?");}
void loop(){}
