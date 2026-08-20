#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin();bot.ask("25 * 4");bot.ask("(12 + 8) / 2");bot.ask("10 / 0");}
void loop(){}
