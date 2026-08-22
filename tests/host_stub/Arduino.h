// =============================================================
// Arduino.h (host test stub)
//
// Minimal subset of the Arduino core API sufficient to compile
// and unit-test the platform-independent parts of AmelTechBot
// (KnowledgeBase, Calculator normalization/matching/parsing logic)
// on a desktop host. Hardware-specific telemetry (ESP.*, WiFi.*,
// Preferences, etc.) is NOT stubbed here — Telemetry.cpp/.h guard
// all such calls behind ARDUINO_ARCH_ESP32/ESP32 macros, which are
// intentionally left undefined in host builds so that code path is
// skipped entirely and reports MEAS_UNSUPPORTED, matching real
// behavior on non-ESP32 targets.
// =============================================================
#ifndef AMELTECH_HOST_STUB_ARDUINO_H
#define AMELTECH_HOST_STUB_ARDUINO_H

#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cctype>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------
// String: minimal Arduino-compatible wrapper around std::string
// ---------------------------------------------------------------
class String {
public:
    String() : _s("") {}
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(char c) { _s = std::string(1, c); }
    String(int v) { _s = std::to_string(v); }
    String(unsigned int v) { _s = std::to_string(v); }
    String(long v) { _s = std::to_string(v); }
    String(unsigned long v) { _s = std::to_string(v); }
    String(double v) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << v;
        _s = oss.str();
    }
    String(float v) : String((double)v) {}

    size_t length() const { return _s.length(); }
    void trim() {
        size_t start = _s.find_first_not_of(" \t\r\n");
        size_t end = _s.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) { _s = ""; return; }
        _s = _s.substr(start, end - start + 1);
    }
    void toLowerCase() {
        for (auto& c : _s) c = (char)tolower((unsigned char)c);
    }
    void toUpperCase() {
        for (auto& c : _s) c = (char)toupper((unsigned char)c);
    }
    String substring(size_t start) const {
        if (start >= _s.length()) return String("");
        return String(_s.substr(start));
    }
    String substring(size_t start, size_t end) const {
        if (start >= _s.length()) return String("");
        if (end > _s.length()) end = _s.length();
        if (end <= start) return String("");
        return String(_s.substr(start, end - start));
    }
    int indexOf(const char* needle) const {
        auto pos = _s.find(needle);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(char c) const {
        auto pos = _s.find(c);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(char c, int fromIndex) const {
        if (fromIndex < 0) fromIndex = 0;
        if ((size_t)fromIndex > _s.length()) return -1;
        auto pos = _s.find(c, (size_t)fromIndex);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(const char* needle, int fromIndex) const {
        if (fromIndex < 0) fromIndex = 0;
        if ((size_t)fromIndex > _s.length()) return -1;
        auto pos = _s.find(needle, (size_t)fromIndex);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    void replace(const String& from, const String& to) {
        if (from._s.empty()) return;
        size_t pos = 0;
        while ((pos = _s.find(from._s, pos)) != std::string::npos) {
            _s.replace(pos, from._s.length(), to._s);
            pos += to._s.length();
        }
    }
    double toDouble() const { return atof(_s.c_str()); }
    int toInt() const { return atoi(_s.c_str()); }
    const char* c_str() const { return _s.c_str(); }
    void toCharArray(char* buf, size_t bufSize) const {
        strncpy(buf, _s.c_str(), bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
    void reserve(size_t n) { _s.reserve(n); }

    char operator[](size_t idx) const { return _s[idx]; }

    String& operator+=(const String& other) { _s += other._s; return *this; }
    String operator+(const String& other) const { return String(_s + other._s); }

    bool operator==(const String& other) const { return _s == other._s; }
    bool operator!=(const String& other) const { return _s != other._s; }

    std::string _s;
};

inline String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs) + rhs._s);
}

// ---------------------------------------------------------------
// PROGMEM stubs: on host, PROGMEM is a no-op and pgm_read_ptr just
// dereferences directly (matches ESP32 behavior, where PROGMEM/
// pgm_read_* are also effectively no-ops on the unified address
// space, which is why knowledge_generated.h works unchanged there).
// ---------------------------------------------------------------
#define PROGMEM
typedef const char* PGM_P;

inline const void* pgm_read_ptr(const void* addr) {
    return *reinterpret_cast<const void* const*>(addr);
}

inline char* strncpy_P(char* dest, PGM_P src, size_t n) {
    return strncpy(dest, src, n);
}

// ---------------------------------------------------------------
// millis()/micros(): deterministic host-side counters
// ---------------------------------------------------------------
inline unsigned long millis() {
    static unsigned long counter = 0;
    return counter += 1; // monotonic, deterministic for tests
}

inline unsigned long micros() {
    static unsigned long counter = 0;
    return counter += 10;
}

inline void delay(unsigned long) {}

// ---------------------------------------------------------------
// isfinite/isalnum/isdigit come from <cmath>/<cctype> already
// ---------------------------------------------------------------
#include <cmath>

// A minimal Serial stub so code referencing Serial.* compiles in
// host tests that happen to include it transitively. AmelTechBot's
// core logic under test (KnowledgeBase, Calculator) does not call
// Serial, so this is intentionally minimal.
class SerialStub {
public:
    unsigned long baudRate() { return 0; } // host build: UNAVAILABLE by design
    void begin(unsigned long) {}
    template <typename T> void print(T) {}
    template <typename T> void println(T) {}
    void println() {}
};
extern SerialStub Serial;

#endif // AMELTECH_HOST_STUB_ARDUINO_H
