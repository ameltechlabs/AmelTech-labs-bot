/*
 * KnowledgeBase.h
 * ---------------------------------------------------------------------------
 * Built-in knowledge (flash, read-only) + user knowledge (heap, persisted to
 * NVS), searched by a two-stage matcher.
 *
 * Stage 1 walks every row using only precomputed 32-bit token signatures and
 * 64-bit trigram sketches: two popcounts per row, no allocation, no
 * tokenisation. Only the best AMELTECH_CANDIDATE_POOL rows reach stage 2, which
 * runs the full blended similarity. This replaces the previous design, which
 * ran a Levenshtein pass over all 2000+ rows for every question.
 *
 * User entries are heap allocated one uniform block at a time. Uniform block
 * sizes let the allocator reuse freed slots exactly, so repeated train/delete
 * cycles do not fragment the heap.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_KNOWLEDGE_BASE_H
#define AMELTECH_KNOWLEDGE_BASE_H

#include <Arduino.h>
#include "AmelTechConfig.h"
#include "NeuralEngine.h"
#include "knowledge_generated.h"

// Result codes returned by addUser().
enum KbAddResult : int8_t {
    KB_ADD_OK = 0,
    KB_ADD_INVALID = -1,
    KB_ADD_DUPLICATE = -2,
    KB_ADD_CONFLICT = -3,
    KB_ADD_FULL = -4,
    KB_ADD_TOO_LARGE = -5,
    KB_ADD_NO_MEMORY = -6,
    KB_ADD_HEAP_GUARD = -7
};

struct UserKnowledgeEntry {
    char question[AMELTECH_MAX_QUESTION_LEN];
    char answer[AMELTECH_MAX_ANSWER_LEN];
    char category[AMELTECH_MAX_CATEGORY_LEN];
    char normalized[AMELTECH_MAX_QUESTION_LEN];
    uint64_t bloom;
    uint32_t signature;
    uint32_t createdMs;
    uint16_t code;        // 4-digit data number handed to the user
    uint8_t tokenCount;
};

struct MatchResult {
    bool found;
    float confidence;        // calibrated 0..1
    float rawScore;          // pre-calibration blend
    const char* answer;
    const char* category;
    const char* matchedQuestion;
    const char* matchedNormalized;
    bool fromUser;
    uint16_t code;           // user entries only, 0 for built-in
    int32_t index;           // row index, -1 when nothing matched
};

class KnowledgeBase {
public:
    KnowledgeBase();
    ~KnowledgeBase();

    bool begin();
    void end();

    // ---- matching -------------------------------------------------------
    MatchResult findBest(const char* question, float minConfidence = AMELTECH_CONF_WEAK);
    MatchResult findBest(const AmelTechQuery& query, float minConfidence = AMELTECH_CONF_WEAK);

    // Fills `out` with up to maxOut ranked matches. Returns how many were set.
    uint8_t rank(const AmelTechQuery& query, MatchResult* out, uint8_t maxOut);

    // ---- user knowledge -------------------------------------------------
    int8_t addUser(const char* question, const char* answer, const char* category,
                   uint16_t* assignedCode = nullptr);
    int8_t removeUser(const char* question);
    int8_t removeUserByCode(uint16_t code);
    void clearUser();

    size_t userCount() const { return _userCount; }
    size_t builtinCount() const { return AMELTECH_BUILTIN_KNOWLEDGE_COUNT; }
    size_t totalCount() const { return builtinCount() + _userCount; }
    uint32_t userHeapBytes() const { return (uint32_t)_userCount * (uint32_t)sizeof(UserKnowledgeEntry); }
    static uint32_t entryHeapCost() { return (uint32_t)sizeof(UserKnowledgeEntry); }

    const UserKnowledgeEntry* userAt(size_t index) const;
    const UserKnowledgeEntry* userByCode(uint16_t code) const;
    uint16_t peekNextCode() const { return _nextCode; }

    // Free heap that must remain after training. 0 disables the guard.
    void setMinFreeHeap(uint32_t bytes) { _minFreeHeap = bytes; }
    uint32_t minFreeHeap() const { return _minFreeHeap; }
    static uint32_t freeHeap();

    // ---- persistence ----------------------------------------------------
    int8_t saveToNvs();
    int8_t loadFromNvs();
    bool isDirty() const { return _dirty; }

    // ---- diagnostics ----------------------------------------------------
    uint32_t lastScanMicros() const { return _lastScanMicros; }
    uint16_t lastCandidateCount() const { return _lastCandidates; }
    bool lastUsedFallbackPass() const { return _lastFallback; }
    uint32_t cacheHits() const { return _cacheHits; }
    uint32_t queryCount() const { return _queries; }

    static const char* addResultToString(int8_t rc);

private:
    UserKnowledgeEntry* _user[AMELTECH_MAX_USER_ENTRIES];
    size_t _userCount;
    uint16_t _nextCode;
    bool _dirty;
    uint32_t _minFreeHeap;

    // Single-slot result cache: repeated identical questions are common in
    // chat and re-scanning costs CPU cycles and therefore heat.
    char _cacheKey[AMELTECH_MAX_QUESTION_LEN];
    MatchResult _cacheValue;
    bool _cacheValid;

    uint32_t _lastScanMicros;
    uint16_t _lastCandidates;
    bool _lastFallback;
    uint32_t _cacheHits;
    uint32_t _queries;

    struct Candidate {
        float pre;
        int32_t index;
        bool user;
    };

    void invalidateCache();
    static bool answersConflict(const char* a, const char* b);
    static void insertCandidate(Candidate* pool, uint8_t& used, uint8_t cap,
                                float pre, int32_t index, bool user);
    void fillResultFromBuiltin(MatchResult& r, int32_t idx) const;
    void fillResultFromUser(MatchResult& r, int32_t idx) const;
    int findUserSlotByCode(uint16_t code) const;
    uint16_t allocateCode();
};

#endif // AMELTECH_KNOWLEDGE_BASE_H
