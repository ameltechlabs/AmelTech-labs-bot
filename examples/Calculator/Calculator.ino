/*
 * Calculator
 * ---------------------------------------------------------------------------
 * The bot understands maths written the way people actually type it, not just
 * strict expressions. This sketch runs a fixed list first, then lets you try
 * your own from the Serial Monitor.
 *
 * Supported: + - * / % ^ ! |x|, implicit multiplication, scientific notation,
 * over thirty functions and the constants pi, e, tau and phi.
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

const char* SAMPLES[] = {
    "25 * 4",
    "(12 + 8) / 5",
    "2^10",
    "-2^2",
    "7!",
    "sqrt(144)",
    "15% of 200",
    "200 + 10%",
    "3(4 + 5)",
    "2pi",
    "|-5|",
    "12 x 4",
    "what is 7 squared",
    "log10(1000)",
    "hypot(3, 4)",
    "gcd(48, 18)",
    "mod(10, 3)",
    "1.5e3 + 500",
    "sin(pi / 2)",
    "10 / 0"
};

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    Serial.println();
    Serial.println("=== Calculator demo ===");
    for (unsigned i = 0; i < sizeof(SAMPLES) / sizeof(SAMPLES[0]); ++i) {
        Serial.print(SAMPLES[i]);
        Serial.print("  =  ");
        Serial.println(bot.calculate(SAMPLES[i]));
    }

    // Trigonometry in degrees, if that suits your users better.
    bot.calculator().setAngleMode(CALC_DEGREES);
    Serial.print("sin(30) in degrees  =  ");
    Serial.println(bot.calculate("sin(30)"));
    bot.calculator().setAngleMode(CALC_RADIANS);

    Serial.println();
    Serial.println("Now type your own expression.");
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
    }
    bot.tick();
}
