#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){
  Serial.begin(115200); bot.begin();
  bot.train("what is an apple","An apple is a fruit.","general");
  bot.train("how many seconds are there in one minute","There are 60 seconds in one minute.","general");
  bot.train("what is water","Water is a chemical compound made of hydrogen and oxygen.","science");
  bot.enableTrolling(true);
  bot.diagnostics().fullScan();
  bot.ask("how many sec r there in 1 min");
  bot.ask("25 * 4");
  bot.ask("How much RAM do I have?");
  bot.ask("Is my Wi-Fi good?");
  bot.ask("Run diagnostics");
}
void loop(){}
