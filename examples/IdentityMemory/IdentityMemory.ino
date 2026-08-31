/*
 * IdentityMemory
 * ---------------------------------------------------------------------------
 * The bot remembers who it is talking to, across resets and power cuts.
 *
 * How it behaves:
 *   1. Say "hi my name is Joky Pk" and the name is saved automatically.
 *   2. Add "I'm an engineering student" and the field is saved too.
 *   3. Reset or power cycle the board. The first thing you type is answered
 *      with "Are you Joky Pk?" using the most recently seen name.
 *   4. Answer "yes" and the bot greets you and then answers the question you
 *      originally asked.
 *   5. Answer "no" and it offers the next remembered name. After four "no"
 *      answers it stops guessing and asks
 *      "How are you,.. What is your name?"
 *   6. Reply "My name is Arjun. I'm an engineering student" and both the name
 *      and the field are saved.
 *
 * Up to 34 names are kept. Saving a new one when full drops the 34th, which is
 * the name that has not been seen for the longest.
 *
 * Other commands: "what is my name", "names", "forget me".
 * ---------------------------------------------------------------------------
 */

#include <AmelTechBot.h>

AmelTechBot bot;

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    Serial.println();
    Serial.print("Names remembered from before: ");
    Serial.print((unsigned long)bot.getUserProfileCount());
    Serial.print(" of ");
    Serial.println(UserProfileStore::capacity());

    if (bot.getUserProfileCount() > 0) {
        Serial.println(bot.listUsers());
        Serial.println("Type anything and I will check who you are.");
    } else {
        Serial.println("I do not know anyone yet.");
        Serial.println("Try: hi my name is Joky Pk");
    }
    Serial.println();
}

void loop() {
    String reply;
    if (bot.pollSerial(Serial, reply)) {
        Serial.println(reply);

        // Show the internal state so the flow is easy to follow.
        const char* who = bot.getUserName();
        if (who) {
            Serial.print("   [currently talking to ");
            Serial.print(who);
            const char* field = bot.getUserField();
            if (field) {
                Serial.print(", ");
                Serial.print(field);
            }
            Serial.println("]");
        }
        Serial.println();
    }

    bot.tick();
}
