/*
 * KnowledgeBase — built-in (flash) + user (RAM/NVS) knowledge
 * Exact, normalized, keyword, fuzzy, lightweight similarity matching
 */

#ifndef AMELTECH_KNOWLEDGE_BASE_H
#define AMELTECH_KNOWLEDGE_BASE_H

#include <Arduino.h>
#include "knowledge_generated.h"

// Limits
#ifndef AMELTECH_MAX_USER_ENTRIES
#define AMELTECH_MAX_USER_ENTRIES 32
#endif
#ifndef AMELTECH_MAX_QUESTION_LEN
#define AMELTECH_MAX_QUESTION_LEN 128
#endif
#ifndef AMELTECH_MAX_ANSWER_LEN
#define AMELTECH_MAX_ANSWER_LEN 384
#endif
#ifndef AMELTECH_MAX_CATEGORY_LEN
#define AMELTECH_MAX_CATEGORY_LEN 32
#endif
#ifndef AMELTECH_MAX_TOKENS
#define AMELTECH_MAX_TOKENS 24
#endif

struct UserKnowledgeEntry {
    char question[AMELTECH_MAX_QUESTION_LEN];
    char answer[AMELTECH_MAX_ANSWER_LEN];
    char category[AMELTECH_MAX_CATEGORY_LEN];
    char normalized[AMELTECH_MAX_QUESTION_LEN];
    bool used;
};

struct MatchResult {
    bool found;
    float confidence;
    const char* answer;      // pointer into built-in or user storage
    const char* category;
    const char* matchedQuestion;
    bool fromUser;
};

class KnowledgeBase {
public:
    KnowledgeBase();

    bool begin();
    void clearUser();

    // Matching
    MatchResult findBest(const char* question, float minConfidence = 0.50f);

    // User training
    int8_t addUser(const char* question, const char* answer, const char* category);
    // Returns: 0=OK, -1=invalid, -2=duplicate, -3=conflict, -4=full, -5=overflow
    int8_t removeUser(const char* question);
    size_t userCount() const;
    size_t builtinCount() const { return AMELTECH_BUILTIN_KNOWLEDGE_COUNT; }
    size_t totalCount() const { return builtinCount() + userCount(); }

    // Persistence
    int8_t saveToNvs();
    int8_t loadFromNvs();

    // Normalization / utilities (public for testing)
    static void normalize(const char* in, char* out, size_t outSize);
    static int tokenize(const char* text, char tokens[][32], int maxTokens);
    static float keywordScore(const char* tokens[], int nTokens, const char* candidateNorm);
    static int levenshtein(const char* a, const char* b, int maxDist);
    static float fuzzyScore(const char* a, const char* b);
    static float lightweightSimilarity(const char* a, const char* b);

private:
    UserKnowledgeEntry _user[AMELTECH_MAX_USER_ENTRIES];
    size_t _userCount;

    MatchResult matchBuiltin(const char* norm, const char* original);
    MatchResult matchUser(const char* norm, const char* original);
    float scoreCandidate(const char* norm, const char* candNorm, const char* candQ);
    bool answersConflict(const char* a, const char* b) const;
};

#endif // AMELTECH_KNOWLEDGE_BASE_H
