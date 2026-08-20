#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){ Serial.begin(115200); bot.begin(); bot.train("what is an apple","An apple is a fruit.","general"); bot.ask("What is an apple?"); bot.ask("how many sec r there in 1 min"); }
void loop(){}
