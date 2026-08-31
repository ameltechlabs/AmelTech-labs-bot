/*
 * AmelTechLog.h
 * ---------------------------------------------------------------------------
 * Bounded, heap-aware diagnostic log.
 *
 * Design rules:
 *  - The ring buffer is allocated lazily and only while enough heap is free.
 *    If memory gets tight the buffer is released and logging degrades to
 *    counters only, so logging can never be the reason a chat reply fails.
 *  - Fixed-size slots: no String concatenation, no fragmentation.
 *  - Oldest entries are overwritten; the log never grows without bound.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_LOG_H
#define AMELTECH_LOG_H

#include <Arduino.h>
#include "AmelTechConfig.h"

enum AmelTechLogLevel : uint8_t {
    AMELTECH_LOG_NONE = 0,
    AMELTECH_LOG_ERROR,
    AMELTECH_LOG_WARN,
    AMELTECH_LOG_INFO,
    AMELTECH_LOG_DEBUG
};

struct AmelTechLogSlot {
    uint32_t timeMs;
    uint32_t freeHeap;
    uint8_t level;
    char text[AMELTECH_LOG_LINE_LEN];
};

class AmelTechLog {
public:
    AmelTechLog();
    ~AmelTechLog();

    void begin(AmelTechLogLevel level = AMELTECH_LOG_WARN);
    void end();

    void setLevel(AmelTechLogLevel level) { _level = level; }
    AmelTechLogLevel level() const { return _level; }

    // Mirror every accepted line to a stream (usually Serial). Optional.
    void setEcho(Stream* stream) { _echo = stream; }

    void log(AmelTechLogLevel level, const char* fmt, ...);

    // Counters survive even when the ring buffer is not allocated.
    uint16_t errorCount() const { return _counts[AMELTECH_LOG_ERROR]; }
    uint16_t warnCount() const { return _counts[AMELTECH_LOG_WARN]; }
    uint16_t infoCount() const { return _counts[AMELTECH_LOG_INFO]; }
    uint16_t droppedCount() const { return _dropped; }

    uint8_t count() const { return _count; }
    bool bufferActive() const { return _slots != nullptr; }

    // idx 0 is the OLDEST retained entry.
    const AmelTechLogSlot* at(uint8_t idx) const;

    String dump(uint8_t maxLines = AMELTECH_LOG_SLOTS) const;
    void clear();

    static const char* levelName(AmelTechLogLevel l);

private:
    AmelTechLogSlot* _slots;
    uint8_t _head;      // next write position
    uint8_t _count;     // valid entries, <= AMELTECH_LOG_SLOTS
    AmelTechLogLevel _level;
    Stream* _echo;
    uint16_t _counts[5];
    uint16_t _dropped;

    bool ensureBuffer();
    void releaseBuffer();
    static uint32_t freeHeapNow();
};

// Shared instance used by the library. Sketches may use it directly.
extern AmelTechLog AmelTechLogger;

#endif // AMELTECH_LOG_H
