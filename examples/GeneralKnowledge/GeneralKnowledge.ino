#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin(); bot.addQA("what is the capital of india","The capital of India is New Delhi.","general"); bot.addQA("what is gravity","Gravity is an attractive interaction between mass and energy.","science"); bot.ask("What is the capital of India?"); bot.ask("Why does Earth rotate?");}
void loop(){}
