/*
 * DhtTrolling
 * ---------------------------------------------------------------------------
 * DHT11 / DHT21 / DHT22 support with automatic situation analysis and a bit of
 * cheek.
 *
 * Wiring (DHT22, three pin breakout):
 *   VCC  -> 3V3
 *   DATA -> GPIO 4   (a 4.7k-10k pull-up to 3V3 is required; most breakout
 *                     boards already have one fitted)
 *   GND  -> GND
 *
 * No external DHT library is needed: the protocol is implemented inside this
 * library so behaviour is identical on every ESP32 core.
 *
 * The humour is only ever added alongside a real reading. If the sensor does
 * not answer, the bot says so plainly and makes no joke and no guess.
 *
 * Try asking:
 *   what is the temperature
 *   how humid is it
 *   how is the room
 *   analyse the situation
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

#define DHT_PIN  4
#define DHT_KIND DHT_TYPE_22    // use DHT_TYPE_11 for the blue DHT11

AmelTechBot bot;
unsigned long lastReport = 0;

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    if (!bot.beginDHT(DHT_PIN, DHT_KIND)) {
        Serial.println("Could not configure the DHT sensor. Check the pin number.");
    }

    // Trolling is on by default; turn it off for a strictly serious device.
    bot.enableTrolling(true);

    Serial.println();
    Serial.println("DHT demo. First reading can take a couple of seconds.");
    delay(2000);

    Serial.println(bot.ask("what is the temperature"));
    Serial.println();
    Serial.println(bot.ask("how humid is it"));
    Serial.println();
    Serial.println(bot.getSituationReport());
}

void loop() {
    if (millis() - lastReport >= 30000) {
        lastReport = millis();

        Serial.println();
        Serial.println("=== 30 second situation update ===");
        Serial.println(bot.getSituationReport());

        // The analysis is also available as structured data.
        SituationReport s = bot.sensors().analyze();
        if (s.valid) {
            Serial.print("dew point ");
            Serial.print(s.dewPointC, 1);
            Serial.print(" C, absolute humidity ");
            Serial.print(s.absoluteHumidity, 1);
            Serial.print(" g/m3, comfort ");
            Serial.println(SensorHub::comfortName(s.comfort));

            if (s.condensationRisk) Serial.println("WARNING: condensation risk.");
            if (s.moldRisk)         Serial.println("WARNING: prolonged high humidity.");
            if (s.electronicsRisk)  Serial.println("WARNING: bad conditions for the board.");
        } else {
            Serial.print("No valid reading: ");
            Serial.println(SensorHub::resultName(bot.getSensorReading().lastResult));
        }

        // The Sensors component of the health score tracks read reliability.
        Serial.println(bot.getHealthReport());
    }

    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);
    }

    bot.tick();
}
