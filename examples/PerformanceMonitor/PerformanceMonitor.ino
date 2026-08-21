#include <AmelTechBot.h>
AmelTechBot bot(&Serial);
void setup(){Serial.begin(115200);bot.begin(); uint32_t avg=bot.diagnostics().benchmarkLoop(32); Serial.print("Measured benchmark loop average: "); Serial.print(avg); Serial.println(" us"); bot.ask("How fast is it?");}
void loop(){}
