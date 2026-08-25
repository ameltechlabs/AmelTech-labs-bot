/*
 * Minimal Arduino stub for host-side unit tests
 */
#ifndef ARDUINO_H_STUB
#define ARDUINO_H_STUB

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <string>

// Basic types
typedef uint8_t byte;
typedef bool boolean;

class String {
public:
    String() : s_() {}
    String(const char* c) : s_(c ? c : "") {}
    String(const std::string& st) : s_(st) {}
    String(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
    String(unsigned int v) { char b[32]; snprintf(b, sizeof(b), "%u", v); s_ = b; }
    String(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); s_ = b; }
    String(float v, int decimals = 2) {
        char b[48];
        snprintf(b, sizeof(b), "%.*f", decimals, v);
        s_ = b;
    }
    String(double v, int decimals = 2) {
        char b[48];
        snprintf(b, sizeof(b), "%.*f", decimals, v);
        s_ = b;
    }

    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator==(const char* o) const { return s_ == (o ? o : ""); }
    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o) { if (o) s_ += o; return *this; }
    String& operator+=(char c) { s_ += c; return *this; }
    String operator+(const String& o) const { return String(s_ + o.s_); }
    String operator+(const char* o) const { return String(s_ + (o ? o : "")); }
    friend String operator+(const char* a, const String& b) {
        return String(std::string(a ? a : "") + b.s_);
    }
    char operator[](size_t i) const { return s_[i]; }
    int indexOf(const char* sub) const {
        size_t p = s_.find(sub ? sub : "");
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String& sub) const { return indexOf(sub.c_str()); }
    String substring(int from, int to) const {
        if (from < 0) from = 0;
        if (to > (int)s_.size()) to = (int)s_.size();
        if (from >= to) return String();
        return String(s_.substr(from, to - from));
    }
    void trim() {
        size_t start = s_.find_first_not_of(" \t\r\n");
        size_t end = s_.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) { s_.clear(); return; }
        s_ = s_.substr(start, end - start + 1);
    }
    int toInt() const { return atoi(s_.c_str()); }
    void reserve(size_t n) { s_.reserve(n); }

private:
    std::string s_;
};

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif
#ifndef strncpy_P
#define strncpy_P strncpy
#endif
#ifndef memcpy_P
#define memcpy_P memcpy
#endif

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

// Serial stub
class HardwareSerial {
public:
    void begin(unsigned long) {}
    void print(const char* s) { fputs(s, stdout); }
    void print(const String& s) { fputs(s.c_str(), stdout); }
    void print(int v) { printf("%d", v); }
    void print(float v, int d = 2) { printf("%.*f", d, v); }
    void println(const char* s) { puts(s); }
    void println(const String& s) { puts(s.c_str()); }
    void println(int v) { printf("%d\n", v); }
    void println() { putchar('\n'); }
    void println(float v, int d = 2) { printf("%.*f\n", d, v); }
};
static HardwareSerial Serial;

#endif
