/*
 * Preferences.h  (host test stub)
 * ---------------------------------------------------------------------------
 * A tiny in-memory stand-in for the ESP32 Preferences (NVS) API so the host
 * tests can exercise the real save and load paths, including the "survives a
 * power cycle" behaviour of the name memory.
 *
 * Storage lives in a process-wide map, so destroying and recreating an
 * AmelTechBot behaves like a reset: RAM is cleared, flash is not.
 *
 * This file is never compiled for the ESP32; the real Preferences library is
 * used there.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_HOST_PREFERENCES_H
#define AMELTECH_HOST_PREFERENCES_H

#include <Arduino.h>
#include <map>
#include <string>

class Preferences {
public:
    Preferences() : _open(false) {}

    bool begin(const char* name, bool readOnly = false) {
        (void)readOnly;
        _ns = name ? name : "";
        _open = true;
        return true;
    }

    void end() { _open = false; }

    bool clear() {
        if (!_open) return false;
        auto& m = store();
        for (auto it = m.begin(); it != m.end();) {
            if (it->first.rfind(_ns + "/", 0) == 0) it = m.erase(it);
            else ++it;
        }
        return true;
    }

    bool remove(const char* key) {
        if (!_open) return false;
        return store().erase(full(key)) > 0;
    }

    bool isKey(const char* key) {
        return store().count(full(key)) > 0;
    }

    size_t putString(const char* key, const char* value) {
        if (!_open) return 0;
        store()[full(key)] = value ? value : "";
        return value ? strlen(value) : 0;
    }
    size_t putString(const char* key, const String& value) {
        return putString(key, value.c_str());
    }

    String getString(const char* key, const String& fallback = String()) {
        auto it = store().find(full(key));
        if (it == store().end()) return fallback;
        return String(it->second.c_str());
    }

    size_t putUChar(const char* key, uint8_t v)   { return putNumber(key, (long long)v); }
    size_t putUShort(const char* key, uint16_t v) { return putNumber(key, (long long)v); }
    size_t putUInt(const char* key, uint32_t v)   { return putNumber(key, (long long)v); }
    size_t putULong(const char* key, uint32_t v)  { return putNumber(key, (long long)v); }
    size_t putInt(const char* key, int32_t v)     { return putNumber(key, (long long)v); }
    size_t putBool(const char* key, bool v)       { return putNumber(key, v ? 1 : 0); }

    uint8_t  getUChar(const char* key, uint8_t d = 0)   { return (uint8_t)getNumber(key, d); }
    uint16_t getUShort(const char* key, uint16_t d = 0) { return (uint16_t)getNumber(key, d); }
    uint32_t getUInt(const char* key, uint32_t d = 0)   { return (uint32_t)getNumber(key, d); }
    uint32_t getULong(const char* key, uint32_t d = 0)  { return (uint32_t)getNumber(key, d); }
    int32_t  getInt(const char* key, int32_t d = 0)     { return (int32_t)getNumber(key, d); }
    bool     getBool(const char* key, bool d = false)   { return getNumber(key, d ? 1 : 0) != 0; }

    size_t putBytes(const char* key, const void* data, size_t len) {
        if (!_open || !data) return 0;
        store()[full(key)] = std::string((const char*)data, len);
        return len;
    }
    size_t getBytes(const char* key, void* out, size_t maxLen) {
        auto it = store().find(full(key));
        if (it == store().end() || !out) return 0;
        size_t n = it->second.size();
        if (n > maxLen) n = maxLen;
        memcpy(out, it->second.data(), n);
        return n;
    }
    size_t getBytesLength(const char* key) {
        auto it = store().find(full(key));
        return (it == store().end()) ? 0 : it->second.size();
    }

    size_t freeEntries() { return 4096 - store().size(); }

    // Test helper: wipes everything, simulating a fresh chip.
    static void hostEraseAll() { store().clear(); }

private:
    std::string _ns;
    bool _open;

    // Deliberately leaked. A global AmelTechBot is destroyed during static
    // teardown, and its destructor saves; if this map were a plain function
    // static it could already be gone by then. The real ESP32 never exits, so
    // this is a host-test concern only.
    static std::map<std::string, std::string>& store() {
        static std::map<std::string, std::string>* s =
            new std::map<std::string, std::string>();
        return *s;
    }

    std::string full(const char* key) const {
        return _ns + "/" + (key ? key : "");
    }

    size_t putNumber(const char* key, long long v) {
        if (!_open) return 0;
        store()[full(key)] = std::to_string(v);
        return sizeof(long long);
    }

    long long getNumber(const char* key, long long fallback) {
        auto it = store().find(full(key));
        if (it == store().end()) return fallback;
        try {
            return std::stoll(it->second);
        } catch (...) {
            return fallback;
        }
    }
};

#endif // AMELTECH_HOST_PREFERENCES_H
