// Calculator.ino
//
// Demonstrates the safe embedded calculator, both through ask()
// (auto-detected arithmetic) and the direct calculate() API.

#include <AmelTechBot.h>

AmelTechBot bot;

void showCalc(const String& expr) {
    CalcResult r = bot.calculate(expr);
    Serial.print(expr);
    Serial.print(" = ");
    if (r.valid) {
        Serial.println(r.value);
    } else {
        Serial.print("ERROR: ");
        Serial.println(r.message);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    showCalc("25 * 4");
    showCalc("100 / 5");
    showCalc("(25 + 5) * 2");
    showCalc("50%");
    showCalc("25 + 10%");
    showCalc("10 / 0");        // division by zero -> error
    showCalc("5 + + 3");       // malformed -> error
    showCalc("5 $ 3");         // invalid character -> error

    // Also works transparently through ask()
    Serial.println(bot.ask("(12 + 8) * 3"));
}

void loop() {
}
