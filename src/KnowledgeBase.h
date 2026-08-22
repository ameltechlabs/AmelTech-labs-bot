// =============================================================
// KnowledgeBase.h
//
// Offline question-answer engine: normalization, tokenization,
// exact/keyword/fuzzy matching, lightweight semantic-style
// similarity, confidence scoring, and contradiction checking.
//
// Built-in knowledge (from knowledge_generated.h, flash-resident)
// is kept separate from user-trained knowledge (RAM + optional
// NVS persistence), per spec item 9.
// =============================================================
#ifndef AMELTECH_KNOWLEDGE_BASE_H
#define AMELTECH_KNOWLEDGE_BASE_H

#include <Arduino.h>
#include "AmelTechTypes.h"

#ifndef AMELTECH_MAX_QUESTION_LEN
#define AMELTECH_MAX_QUESTION_LEN 96
#endif
#ifndef AMELTECH_MAX_ANSWER_LEN
#define AMELTECH_MAX_ANSWER_LEN 220
#endif
#ifndef AMELTECH_MAX_CATEGORY_LEN
#define AMELTECH_MAX_CATEGORY_LEN 24
#endif
#ifndef AMELTECH_MAX_USER_ENTRIES
#define AMELTECH_MAX_USER_ENTRIES 64
#endif
#ifndef AMELTECH_MAX_TOKENS
#define AMELTECH_MAX_TOKENS 16
#endif

// A single knowledge entry as materialized in RAM for matching
// (built-in entries are copied out of flash on demand; user
// entries live in RAM directly, bounded by AMELTECH_MAX_USER_ENTRIES).
struct KnowledgeEntry {
    char question[AMELTECH_MAX_QUESTION_LEN + 1];
    char answer[AMELTECH_MAX_ANSWER_LEN + 1];
    char category[AMELTECH_MAX_CATEGORY_LEN + 1];
    bool isUserEntry;
};

// Result of a knowledge lookup.
struct MatchResult {
    bool found;
    float confidence;          // 0.0 - 1.0
    String answer;
    String category;
    String matchedQuestion;    // the stored question that matched
    bool fromUserKnowledge;
};

// Result of a train()/addQA() call.
struct TrainResult {
    AmelTechStatus status;
    String message;
};

class KnowledgeBase {
public:
    KnowledgeBase();

    // Loads built-in knowledge references (flash-resident, not copied
    // into RAM) and initializes the empty user knowledge table.
    void begin();

    // ---------------- Matching ----------------
    // Runs the full matching pipeline: normalization -> tokenization
    // -> exact match -> keyword/fuzzy/similarity scoring -> best
    // candidate selection with confidence.
    MatchResult query(const String& rawInput) const;

    // ---------------- Training / user knowledge ----------------
    TrainResult train(const String& question, const String& answer, const String& category);
    TrainResult addQA(const String& question, const String& answer, const String& category);
    AmelTechStatus removeQA(const String& question);
    void clearUserKnowledge();

    size_t builtInCount() const;
    size_t userCount() const;
    size_t totalCount() const;

    // ---------------- Persistence ----------------
    AmelTechStatus saveToNVS() const;
    AmelTechStatus loadFromNVS();

    // ---------------- Utilities exposed for testing ----------------
    static String normalize(const String& input);
    static void tokenize(const String& normalized, String outTokens[AMELTECH_MAX_TOKENS], uint8_t& outCount);
    static float similarity(const String& a, const String& b); // lightweight token-overlap + edit-distance blend
    static uint16_t levenshtein(const String& a, const String& b, uint16_t maxDistance);

private:
    KnowledgeEntry _userEntries[AMELTECH_MAX_USER_ENTRIES];
    uint8_t _userCount;

    // Internal candidate scoring against both built-in (flash) and
    // user (RAM) entries.
    bool _scoreAgainst(const String& normalizedInput, const String inputTokens[AMELTECH_MAX_TOKENS],
                        uint8_t inputTokenCount, const char* candidateQuestion, float& outScore) const;

    int _findUserEntryIndex(const String& normalizedQuestion) const;
    bool _isNormalizedDuplicateOfBuiltIn(const String& normalizedQuestion, String& outExistingAnswer) const;
    bool _isValidCategory(const String& category) const;
};

#endif // AMELTECH_KNOWLEDGE_BASE_H
