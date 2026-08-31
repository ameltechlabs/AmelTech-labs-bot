/*
 * WiFiDiagnostics
 * ---------------------------------------------------------------------------
 * The bot never needs Wi-Fi, but it can report on it when a connection exists.
 *
 * Fill in your credentials below to see live signal reporting. Leave them
 * empty and the sketch demonstrates that everything still works offline, with
 * Wi-Fi honestly reported as disconnected rather than guessed at.
 * ---------------------------------------------------------------------------
 */

#include <WiFi.h>
#include <AmelTechBot.h>

const char* WIFI_SSID = "";      // your network name, or leave empty
const char* WIFI_PASSWORD = "";  // your network password

AmelTechBot bot;
unsigned long lastReport = 0;

void setup() {
    Serial.begin(115200);
    delay(300);
    bot.begin();

    if (strlen(WIFI_SSID) > 0) {
        Serial.print("Connecting to ");
        Serial.println(WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long deadline = millis() + 15000;
        while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
            delay(250);
            Serial.print('.');
            bot.tick();
        }
        Serial.println();
    } else {
        Serial.println("No credentials set: running fully offline.");
    }

    Serial.println(bot.ask("wifi status"));
    Serial.println();
    Serial.println(bot.ask("are you online"));
}

void loop() {
    if (millis() - lastReport >= 10000) {
        lastReport = millis();

        const ESP32Telemetry& t = bot.getTelemetry(true);

        Serial.println();
        Serial.print("Connected      : ");
        Serial.println(Telemetry::statusIsUsable(t.wifiConnected.status)
                           ? (t.wifiConnected.value ? "yes" : "no")
                           : Telemetry::statusToString(t.wifiConnected.status));

        if (Telemetry::statusIsUsable(t.wifiRssi.status)) {
            Serial.print("RSSI           : ");
            Serial.print(t.wifiRssi.value);
            Serial.println(" dBm");
        }
        if (t.wifiSsidStatus == MEAS_LIVE) {
            Serial.print("SSID           : ");
            Serial.println(t.wifiSsid);
        }
        if (Telemetry::statusIsUsable(t.wifiConnected.status) && t.wifiConnected.value) {
            Serial.print("IP address     : ");
            Serial.println(WiFi.localIP());
        }
        if (Telemetry::statusIsUsable(t.wifiDisconnectCount.status)) {
            Serial.print("Disconnections : ");
            Serial.println(t.wifiDisconnectCount.value);
        }

        // The Wi-Fi component of the health score, in context.
        Serial.println(bot.getHealthReport());
    }

    bot.tick();
}
