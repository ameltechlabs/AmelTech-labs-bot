#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin();bot.train("what is wifi","Wi-Fi is a wireless networking technology.","general");bot.ask("what is wifi");bot.ask("How much RAM do I have?");bot.ask("Is my Wi-Fi good?");}
void loop(){}
