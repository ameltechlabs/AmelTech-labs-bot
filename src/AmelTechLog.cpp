#include "AmelTechLog.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

AmelTechLog AmelTechLogger;

AmelTechLog::AmelTechLog()
    : _slots(nullptr),
      _head(0),
      _count(0),
      _level(AMELTECH_LOG_NONE),
      _echo(nullptr),
      _dropped(0) {
    memset(_counts, 0, sizeof(_counts));
}

AmelTechLog::~AmelTechLog() {
    releaseBuffer();
}

uint32_t AmelTechLog::freeHeapNow() {
#if defined(ESP32)
    return ESP.getFreeHeap();
#else
    return 0xFFFFFFFFUL;  // host build: never memory constrained
#endif
}

const char* AmelTechLog::levelName(AmelTechLogLevel l) {
    switch (l) {
        case AMELTECH_LOG_ERROR: return "ERROR";
        case AMELTECH_LOG_WARN:  return "WARN";
        case AMELTECH_LOG_INFO:  return "INFO";
        case AMELTECH_LOG_DEBUG: return "DEBUG";
        default: return "NONE";
    }
}

void AmelTechLog::begin(AmelTechLogLevel level) {
    _level = level;
    if (level != AMELTECH_LOG_NONE) ensureBuffer();
}

void AmelTechLog::end() {
    _level = AMELTECH_LOG_NONE;
    releaseBuffer();
}

bool AmelTechLog::ensureBuffer() {
    if (_slots) return true;
    if (freeHeapNow() < AMELTECH_LOG_MIN_FREE_HEAP + sizeof(AmelTechLogSlot) * AMELTECH_LOG_SLOTS) {
        return false;
    }
    _slots = (AmelTechLogSlot*)calloc(AMELTECH_LOG_SLOTS, sizeof(AmelTechLogSlot));
    _head = 0;
    _count = 0;
    return _slots != nullptr;
}

void AmelTechLog::releaseBuffer() {
    if (_slots) {
        free(_slots);
        _slots = nullptr;
    }
    _head = 0;
    _count = 0;
}

void AmelTechLog::clear() {
    if (_slots) memset(_slots, 0, sizeof(AmelTechLogSlot) * AMELTECH_LOG_SLOTS);
    _head = 0;
    _count = 0;
    memset(_counts, 0, sizeof(_counts));
    _dropped = 0;
}

void AmelTechLog::log(AmelTechLogLevel level, const char* fmt, ...) {
    if (level == AMELTECH_LOG_NONE || level > _level) return;
    if (!fmt) return;

    if (level <= 4) _counts[level]++;

    char line[AMELTECH_LOG_LINE_LEN];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) {
        line[0] = '\0';
    }

    uint32_t heap = freeHeapNow();

    if (_echo) {
        _echo->print('[');
        _echo->print(levelName(level));
        _echo->print("] ");
        _echo->println(line);
    }

    // Under memory pressure keep counters but stop holding text.
    if (heap < AMELTECH_LOG_MIN_FREE_HEAP) {
        releaseBuffer();
        _dropped++;
        return;
    }
    if (!ensureBuffer()) {
        _dropped++;
        return;
    }

    AmelTechLogSlot& s = _slots[_head];
    s.timeMs = millis();
    s.freeHeap = heap;
    s.level = (uint8_t)level;
    strncpy(s.text, line, sizeof(s.text) - 1);
    s.text[sizeof(s.text) - 1] = '\0';

    _head = (uint8_t)((_head + 1) % AMELTECH_LOG_SLOTS);
    if (_count < AMELTECH_LOG_SLOTS) _count++;
}

const AmelTechLogSlot* AmelTechLog::at(uint8_t idx) const {
    if (!_slots || idx >= _count) return nullptr;
    uint8_t oldest = (uint8_t)((_head + AMELTECH_LOG_SLOTS - _count) % AMELTECH_LOG_SLOTS);
    uint8_t pos = (uint8_t)((oldest + idx) % AMELTECH_LOG_SLOTS);
    return &_slots[pos];
}

String AmelTechLog::dump(uint8_t maxLines) const {
    String out;
    if (!_slots || _count == 0) {
        out += F("Log buffer empty (errors=");
        out += _counts[AMELTECH_LOG_ERROR];
        out += F(" warnings=");
        out += _counts[AMELTECH_LOG_WARN];
        out += F(" dropped=");
        out += _dropped;
        out += F(")");
        return out;
    }
    uint8_t n = _count;
    if (maxLines < n) n = maxLines;
    out.reserve((size_t)n * 48u + 48u);
    uint8_t start = (uint8_t)(_count - n);
    for (uint8_t i = 0; i < n; ++i) {
        const AmelTechLogSlot* s = at((uint8_t)(start + i));
        if (!s) continue;
        out += (uint32_t)(s->timeMs / 1000);
        out += F("s [");
        out += levelName((AmelTechLogLevel)s->level);
        out += F("] ");
        out += s->text;
        out += '\n';
    }
    out += F("(errors=");
    out += _counts[AMELTECH_LOG_ERROR];
    out += F(" warnings=");
    out += _counts[AMELTECH_LOG_WARN];
    out += F(" dropped=");
    out += _dropped;
    out += ')';
    return out;
}
