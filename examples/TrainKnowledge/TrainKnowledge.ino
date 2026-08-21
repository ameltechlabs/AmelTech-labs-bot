#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin(); Serial.println(bot.train("what is water","Water is a chemical compound made of hydrogen and oxygen.","science")); Serial.println(bot.getKnowledgeCount()); bot.saveKnowledge();}
void loop(){}
