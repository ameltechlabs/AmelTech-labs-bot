// WiFiDiagnostics.ino
//
// Demonstrates Wi-Fi telemetry: connection status, RSSI, and the
// explicit distinction between configured PHY rate (not fabricated
// here, reported UNAVAILABLE unless actually exposed) and measured
// throughput (also UNAVAILABLE unless a benchmark is explicitly run).
//
// Connect to Wi-Fi first for live RSSI data; otherwise fields will
// correctly report UNAVAILABLE rather than fabricated values.

#include <WiFi.h>
#include <AmelTechBot.h>

AmelTechBot bot;

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

void setup() {
    Serial.begin(115200);
    delay(500);

    bot.begin();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    Serial.println(bot.ask("What is my Wi-Fi RSSI?"));
    Serial.print("Measurement status: ");
    Serial.println(measurementStatusToString(bot.getMeasurementStatus()));

    Serial.println(bot.ask("is that good?")); // context-aware follow-up

    ESP32Telemetry t = bot.getTelemetry(false);
    Serial.print("Configured link rate status: ");
    Serial.println(measurementStatusToString(t.wifi.configuredLinkRateMbps.status));
    Serial.print("Measured TX throughput status: ");
    Serial.println(measurementStatusToString(t.wifi.measuredTxThroughputKbps.status));
}

void loop() {
}
