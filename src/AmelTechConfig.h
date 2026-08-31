/*
 * AmelTechConfig.h
 * ---------------------------------------------------------------------------
 * Central compile-time configuration for AmelTech lab's bot.
 *
 * Every tunable in the library lives here so a sketch can override any of them
 * with a -D flag or a #define placed BEFORE #include <AmelTechBot.h>.
 * Nothing in this file allocates memory or emits code.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_CONFIG_H
#define AMELTECH_CONFIG_H

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------
#define AMELTECH_VERSION_MAJOR 2
#define AMELTECH_VERSION_MINOR 0
#define AMELTECH_VERSION_PATCH 0
#define AMELTECH_VERSION_STRING "2.0.0"

// ---------------------------------------------------------------------------
// Text buffer limits (bytes, including the terminating NUL)
// ---------------------------------------------------------------------------
#ifndef AMELTECH_MAX_QUESTION_LEN
#define AMELTECH_MAX_QUESTION_LEN 128
#endif

#ifndef AMELTECH_MAX_ANSWER_LEN
#define AMELTECH_MAX_ANSWER_LEN 320
#endif

#ifndef AMELTECH_MAX_CATEGORY_LEN
#define AMELTECH_MAX_CATEGORY_LEN 32
#endif

#ifndef AMELTECH_MAX_TOKENS
#define AMELTECH_MAX_TOKENS 24
#endif

#ifndef AMELTECH_MAX_TOKEN_LEN
#define AMELTECH_MAX_TOKEN_LEN 24
#endif

// ---------------------------------------------------------------------------
// User knowledge (trainable Q&A)
// ---------------------------------------------------------------------------
// User entries are allocated on the heap one fixed-size block at a time.
// Uniform block size keeps the allocator from fragmenting.
#ifndef AMELTECH_MAX_USER_ENTRIES
#define AMELTECH_MAX_USER_ENTRIES 48
#endif

// Minimum free heap that must REMAIN AVAILABLE for chat logging and normal
// operation. Training is refused when free heap would drop below this.
// Requirement: keep 200 KB of free heap for the chat path.
#ifndef AMELTECH_TRAIN_MIN_FREE_HEAP
#define AMELTECH_TRAIN_MIN_FREE_HEAP (200UL * 1024UL)
#endif

// First data number code handed out by the training system ("0001").
#ifndef AMELTECH_TRAIN_FIRST_CODE
#define AMELTECH_TRAIN_FIRST_CODE 1
#endif

// Codes wrap at 4 digits.
#ifndef AMELTECH_TRAIN_MAX_CODE
#define AMELTECH_TRAIN_MAX_CODE 9999
#endif

// ---------------------------------------------------------------------------
// Identity / user profile memory
// ---------------------------------------------------------------------------
// Requirement: a maximum of 34 names are remembered. Saving name 35 evicts the
// 34th (oldest / least recently seen) entry automatically.
#ifndef AMELTECH_MAX_PROFILES
#define AMELTECH_MAX_PROFILES 34
#endif

#ifndef AMELTECH_PROFILE_NAME_LEN
#define AMELTECH_PROFILE_NAME_LEN 32
#endif

#ifndef AMELTECH_PROFILE_FIELD_LEN
#define AMELTECH_PROFILE_FIELD_LEN 48
#endif

// How many stored names the bot may offer as "Are you <name>?" before it gives
// up and asks the open question instead.
#ifndef AMELTECH_IDENTITY_MAX_GUESSES
#define AMELTECH_IDENTITY_MAX_GUESSES 4
#endif

// After the identity is confirmed the bot greets by name on the next reply and
// then again every N replies, so it stays personal without becoming repetitive.
#ifndef AMELTECH_NAME_MENTION_GAP
#define AMELTECH_NAME_MENTION_GAP 3
#endif

// ---------------------------------------------------------------------------
// Matching engine
// ---------------------------------------------------------------------------
// Hashed-feature vector width used by the deterministic neural scorer.
#ifndef AMELTECH_NEURAL_DIM
#define AMELTECH_NEURAL_DIM 48
#endif

// How many prefiltered candidates survive to the expensive scoring stage.
#ifndef AMELTECH_CANDIDATE_POOL
#define AMELTECH_CANDIDATE_POOL 12
#endif

// Feed the task watchdog every N knowledge rows during a scan.
#ifndef AMELTECH_SCAN_YIELD_INTERVAL
#define AMELTECH_SCAN_YIELD_INTERVAL 256
#endif

// Confidence bands.
#define AMELTECH_CONF_STRONG   0.90f
#define AMELTECH_CONF_MODERATE 0.74f
#define AMELTECH_CONF_WEAK     0.52f

// ---------------------------------------------------------------------------
// Telemetry caching (reduces radio + CPU work, and therefore heat)
// ---------------------------------------------------------------------------
#ifndef AMELTECH_TELEM_FAST_MIN_INTERVAL_MS
#define AMELTECH_TELEM_FAST_MIN_INTERVAL_MS 250
#endif

#ifndef AMELTECH_TELEM_WIFI_MIN_INTERVAL_MS
#define AMELTECH_TELEM_WIFI_MIN_INTERVAL_MS 1000
#endif

#ifndef AMELTECH_TELEM_FULL_MIN_INTERVAL_MS
#define AMELTECH_TELEM_FULL_MIN_INTERVAL_MS 2000
#endif

// ---------------------------------------------------------------------------
// Thermal guard
// ---------------------------------------------------------------------------
// Chip die temperature thresholds in degrees Celsius.
#ifndef AMELTECH_THERMAL_WARN_C
#define AMELTECH_THERMAL_WARN_C 70.0f
#endif

#ifndef AMELTECH_THERMAL_HIGH_C
#define AMELTECH_THERMAL_HIGH_C 80.0f
#endif

#ifndef AMELTECH_THERMAL_CRITICAL_C
#define AMELTECH_THERMAL_CRITICAL_C 90.0f
#endif

// Hysteresis applied before leaving a hot state, so the policy cannot flap.
#ifndef AMELTECH_THERMAL_HYSTERESIS_C
#define AMELTECH_THERMAL_HYSTERESIS_C 4.0f
#endif

// Maximum uninterrupted compute slice before the engine yields to the RTOS.
#ifndef AMELTECH_MAX_COMPUTE_SLICE_MS
#define AMELTECH_MAX_COMPUTE_SLICE_MS 12
#endif

// Reduced clock used when the guard throttles. 80 MHz keeps Wi-Fi usable.
#ifndef AMELTECH_THROTTLE_CPU_MHZ
#define AMELTECH_THROTTLE_CPU_MHZ 80
#endif

// ---------------------------------------------------------------------------
// DHT sensor
// ---------------------------------------------------------------------------
#ifndef AMELTECH_DHT11_MIN_INTERVAL_MS
#define AMELTECH_DHT11_MIN_INTERVAL_MS 1100
#endif

#ifndef AMELTECH_DHT22_MIN_INTERVAL_MS
#define AMELTECH_DHT22_MIN_INTERVAL_MS 2100
#endif

#ifndef AMELTECH_DHT_MAX_RETRIES
#define AMELTECH_DHT_MAX_RETRIES 3
#endif

// Number of samples kept for trend analysis.
#ifndef AMELTECH_DHT_HISTORY
#define AMELTECH_DHT_HISTORY 8
#endif

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
#ifndef AMELTECH_LOG_SLOTS
#define AMELTECH_LOG_SLOTS 16
#endif

#ifndef AMELTECH_LOG_LINE_LEN
#define AMELTECH_LOG_LINE_LEN 80
#endif

// The ring buffer is only allocated while at least this much heap is free.
#ifndef AMELTECH_LOG_MIN_FREE_HEAP
#define AMELTECH_LOG_MIN_FREE_HEAP (24UL * 1024UL)
#endif

// ---------------------------------------------------------------------------
// Conversation context
// ---------------------------------------------------------------------------
#ifndef AMELTECH_MAX_CONTEXT
#define AMELTECH_MAX_CONTEXT 4
#endif

// Text kept per remembered turn. Only what the follow-up handler needs.
#ifndef AMELTECH_CONTEXT_TEXT_LEN
#define AMELTECH_CONTEXT_TEXT_LEN 72
#endif

#ifndef AMELTECH_CONTEXT_TOPIC_LEN
#define AMELTECH_CONTEXT_TOPIC_LEN 48
#endif

// Length of the human readable status string exposed by getLastStatus().
#ifndef AMELTECH_STATUS_LEN
#define AMELTECH_STATUS_LEN 72
#endif

// Dirty state is flushed to flash at most this often. Flash endurance is
// finite, so nothing is written on every single change.
#ifndef AMELTECH_AUTOSAVE_INTERVAL_MS
#define AMELTECH_AUTOSAVE_INTERVAL_MS 60000UL
#endif

// ---------------------------------------------------------------------------
// Calculator
// ---------------------------------------------------------------------------
#ifndef AMELTECH_CALC_MAX_EXPR
#define AMELTECH_CALC_MAX_EXPR 192
#endif

// Guards against stack exhaustion from deeply nested parentheses.
#ifndef AMELTECH_CALC_MAX_DEPTH
#define AMELTECH_CALC_MAX_DEPTH 24
#endif

// Guards against pathological inputs burning CPU (and generating heat).
#ifndef AMELTECH_CALC_MAX_OPS
#define AMELTECH_CALC_MAX_OPS 512
#endif

// ---------------------------------------------------------------------------
// NVS namespaces
// ---------------------------------------------------------------------------
#ifndef AMELTECH_NVS_KB
#define AMELTECH_NVS_KB "ameltech_kb"
#endif

// ---------------------------------------------------------------------------
// Non-volatile storage availability
// ---------------------------------------------------------------------------
// On the ESP32 this is the real NVS partition. Host builds can opt in with
// -DAMELTECH_HOST_NVS so the test suite exercises the same save and load code.
#if defined(ESP32) || defined(AMELTECH_HOST_NVS)
#define AMELTECH_NVS_AVAILABLE 1
#else
#define AMELTECH_NVS_AVAILABLE 0
#endif

#ifndef AMELTECH_NVS_ID
#define AMELTECH_NVS_ID "ameltech_id"
#endif

// ---------------------------------------------------------------------------
// Portability helper: yield to the scheduler without pulling in FreeRTOS here.
// ---------------------------------------------------------------------------
#if defined(ESP32)
#define AMELTECH_YIELD() do { yield(); } while (0)
#else
#define AMELTECH_YIELD() do { } while (0)
#endif

#endif // AMELTECH_CONFIG_H
