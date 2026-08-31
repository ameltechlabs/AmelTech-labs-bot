/*
 * Arduino.h — minimal host stub for AmelTech lab's bot unit tests.
 *
 * This is NOT part of the library. It exists so the pure-logic units
 * (text normalisation, matching, calculator, identity, training) can be built
 * and tested with a normal C++ compiler, with no ESP32 hardware in sight.
 * Anything guarded by #if defined(ESP32) is deliberately absent here.
 */
#ifndef ARDUINO_H_HOST_STUB
#define ARDUINO_H_HOST_STUB

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <string>
#include <chrono>

typedef uint8_t byte;
typedef bool boolean;

// ---------------------------------------------------------------------------
class String {
public:
    String() {}
    String(const char* c) : s_(c ? c : "") {}
    String(const std::string& v) : s_(v) {}
    String(char c) { s_ = std::string(1, c); }
    String(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); s_ = b; }
    String(unsigned int v) { char b[32]; snprintf(b, sizeof(b), "%u", v); s_ = b; }
    String(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); s_ = b; }
    String(unsigned long v) { char b[32]; snprintf(b, sizeof(b), "%lu", v); s_ = b; }
    String(float v, int decimals = 2) { char b[64]; snprintf(b, sizeof(b), "%.*f", decimals, (double)v); s_ = b; }
    String(double v, int decimals = 2) { char b[64]; snprintf(b, sizeof(b), "%.*f", decimals, v); s_ = b; }

    const char* c_str() const { return s_.c_str(); }
    size_t length() const { return s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    void reserve(size_t n) { s_.reserve(n); }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator==(const char* o) const { return s_ == (o ? o : ""); }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator!=(const char* o) const { return s_ != (o ? o : ""); }
    bool equals(const String& o) const { return s_ == o.s_; }
    bool equals(const char* o) const { return s_ == (o ? o : ""); }
    bool equalsIgnoreCase(const String& o) const {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i) {
            if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
        }
        return true;
    }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* o) { if (o) s_ += o; return *this; }
    String& operator+=(char c) { s_ += c; return *this; }
    String& operator+=(int v) { s_ += String(v).s_; return *this; }
    String& operator+=(unsigned int v) { s_ += String(v).s_; return *this; }
    String& operator+=(long v) { s_ += String(v).s_; return *this; }
    String& operator+=(unsigned long v) { s_ += String(v).s_; return *this; }
    String& operator+=(double v) { s_ += String(v).s_; return *this; }

    String operator+(const String& o) const { return String(s_ + o.s_); }
    String operator+(const char* o) const { return String(s_ + (o ? o : "")); }
    String operator+(char c) const { return String(s_ + std::string(1, c)); }
    friend String operator+(const char* a, const String& b) {
        return String(std::string(a ? a : "") + b.s_);
    }

    char operator[](size_t i) const { return i < s_.size() ? s_[i] : '\0'; }
    char charAt(size_t i) const { return (*this)[i]; }

    int indexOf(char c, size_t from = 0) const {
        size_t p = s_.find(c, from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const char* sub, size_t from = 0) const {
        size_t p = s_.find(sub ? sub : "", from);
        return p == std::string::npos ? -1 : (int)p;
    }
    int indexOf(const String& sub, size_t from = 0) const { return indexOf(sub.c_str(), from); }
    int lastIndexOf(char c) const {
        size_t p = s_.rfind(c);
        return p == std::string::npos ? -1 : (int)p;
    }
    int lastIndexOf(const char* sub) const {
        size_t p = s_.rfind(sub ? sub : "");
        return p == std::string::npos ? -1 : (int)p;
    }

    String substring(int from) const {
        if (from < 0) from = 0;
        if (from >= (int)s_.size()) return String();
        return String(s_.substr(from));
    }
    String substring(int from, int to) const {
        if (from < 0) from = 0;
        if (to > (int)s_.size()) to = (int)s_.size();
        if (from >= to) return String();
        return String(s_.substr(from, to - from));
    }

    bool startsWith(const char* p) const {
        if (!p) return false;
        size_t n = strlen(p);
        return s_.size() >= n && s_.compare(0, n, p) == 0;
    }
    bool startsWith(const String& p) const { return startsWith(p.c_str()); }
    bool endsWith(const char* p) const {
        if (!p) return false;
        size_t n = strlen(p);
        return s_.size() >= n && s_.compare(s_.size() - n, n, p) == 0;
    }

    void trim() {
        size_t a = s_.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { s_.clear(); return; }
        size_t b = s_.find_last_not_of(" \t\r\n");
        s_ = s_.substr(a, b - a + 1);
    }
    void toLowerCase() {
        for (size_t i = 0; i < s_.size(); ++i) s_[i] = (char)tolower((unsigned char)s_[i]);
    }
    void toUpperCase() {
        for (size_t i = 0; i < s_.size(); ++i) s_[i] = (char)toupper((unsigned char)s_[i]);
    }
    void remove(size_t index) { if (index < s_.size()) s_.erase(index); }
    void remove(size_t index, size_t count) { if (index < s_.size()) s_.erase(index, count); }
    void replace(const char* from, const char* to) {
        if (!from || !to || !*from) return;
        std::string f(from), t(to);
        size_t pos = 0;
        while ((pos = s_.find(f, pos)) != std::string::npos) {
            s_.replace(pos, f.size(), t);
            pos += t.size();
        }
    }
    void replace(char from, char to) {
        for (size_t i = 0; i < s_.size(); ++i) if (s_[i] == from) s_[i] = to;
    }

    int toInt() const { return atoi(s_.c_str()); }
    float toFloat() const { return (float)atof(s_.c_str()); }
    double toDouble() const { return atof(s_.c_str()); }

private:
    std::string s_;
};

// ---------------------------------------------------------------------------
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
#ifndef F
#define F(x) (x)
#endif

// ---------------------------------------------------------------------------
inline unsigned long millis() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (unsigned long)duration_cast<milliseconds>(steady_clock::now() - t0).count();
}
inline unsigned long micros() {
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return (unsigned long)duration_cast<microseconds>(steady_clock::now() - t0).count();
}
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}
inline void yield() {}

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 1; }
inline void noInterrupts() {}
inline void interrupts() {}

// ---------------------------------------------------------------------------
class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) { fputc(c, stdout); return 1; }
    void print(const char* s) { if (s) fputs(s, stdout); }
    void print(const String& s) { fputs(s.c_str(), stdout); }
    void print(char c) { fputc(c, stdout); }
    void print(int v) { printf("%d", v); }
    void print(unsigned int v) { printf("%u", v); }
    void print(long v) { printf("%ld", v); }
    void print(unsigned long v) { printf("%lu", v); }
    void print(double v, int d = 2) { printf("%.*f", d, v); }
    void println() { fputc('\n', stdout); }
    void println(const char* s) { if (s) fputs(s, stdout); fputc('\n', stdout); }
    void println(const String& s) { fputs(s.c_str(), stdout); fputc('\n', stdout); }
    void println(char c) { fputc(c, stdout); fputc('\n', stdout); }
    void println(int v) { printf("%d\n", v); }
    void println(unsigned int v) { printf("%u\n", v); }
    void println(long v) { printf("%ld\n", v); }
    void println(unsigned long v) { printf("%lu\n", v); }
    void println(double v, int d = 2) { printf("%.*f\n", d, v); }
    void printf_(const char* f, ...) { (void)f; }
    void flush() { fflush(stdout); }
};

class Stream : public Print {
public:
    virtual int available() { return 0; }
    virtual int read() { return -1; }
    virtual int peek() { return -1; }
    virtual String readStringUntil(char) { return String(); }
    void setTimeout(unsigned long) {}
};

class HardwareSerial : public Stream {
public:
    void begin(unsigned long) {}
    void begin(unsigned long, int) {}
    operator bool() const { return true; }
};
extern HardwareSerial Serial;

#endif // ARDUINO_H_HOST_STUB
