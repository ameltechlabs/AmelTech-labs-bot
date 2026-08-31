/*
 * WiFi.h  (host test stub)
 * ---------------------------------------------------------------------------
 * Just enough of the ESP32 WiFi API for the example sketches to be compile
 * checked on a desktop. It never connects to anything: every call reports a
 * disconnected radio, which is exactly the state the library is designed to
 * report honestly.
 *
 * Not used on the ESP32; the real WiFi library is used there.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_HOST_WIFI_H
#define AMELTECH_HOST_WIFI_H

#include <Arduino.h>

enum wl_status_t {
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
};

class IPAddress {
public:
    IPAddress() : _a(0), _b(0), _c(0), _d(0) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        : _a(a), _b(b), _c(c), _d(d) {}

    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                 (unsigned)_a, (unsigned)_b, (unsigned)_c, (unsigned)_d);
        return String(buf);
    }
    operator String() const { return toString(); }

private:
    uint8_t _a, _b, _c, _d;
};

class HostWiFiClass {
public:
    void begin(const char* ssid, const char* password = nullptr) {
        (void)ssid;
        (void)password;
    }
    void disconnect(bool wifiOff = false) { (void)wifiOff; }
    wl_status_t status() const { return WL_DISCONNECTED; }
    int32_t RSSI() const { return 0; }
    String SSID() const { return String(); }
    IPAddress localIP() const { return IPAddress(); }
    String macAddress() const { return String("00:00:00:00:00:00"); }
    void mode(int m) { (void)m; }
};

extern HostWiFiClass WiFi;

#endif // AMELTECH_HOST_WIFI_H
