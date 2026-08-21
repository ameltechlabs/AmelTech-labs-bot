#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin();bot.ask("Is my Wi-Fi good?");}
void loop(){delay(5000);bot.ask("Is my Wi-Fi good?");}
