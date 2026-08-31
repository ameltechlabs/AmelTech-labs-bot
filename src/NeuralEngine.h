/*
 * NeuralEngine.h
 * ---------------------------------------------------------------------------
 * The similarity core used by the knowledge matcher.
 *
 * WHAT THIS IS
 *   A small-language-model-*style* scorer built from feature hashing. Each
 *   question is projected into a fixed-width vector using the hashing trick
 *   (the same idea behind an embedding lookup, minus the learned weights), and
 *   similarity is the cosine between vectors, blended with weighted token
 *   overlap, character-trigram Dice and a bounded edit distance.
 *
 * WHAT THIS IS NOT
 *   It is not a trained neural network and it does not generate text. Every
 *   score is a pure function of the input strings: the same question always
 *   produces the same answer with the same confidence. Matching is fully
 *   deterministic and heuristic, by design, so behaviour on a microcontroller
 *   is reproducible and auditable.
 *
 * COST
 *   Stage 1 (whole knowledge base) is two popcounts per row and needs no
 *   allocation. Stage 2 runs only on the best handful of candidates.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_NEURAL_ENGINE_H
#define AMELTECH_NEURAL_ENGINE_H

#include <Arduino.h>
#include "AmelTechConfig.h"

// ---------------------------------------------------------------------------
// Shared text utilities. The Python generator in tools/ implements exactly the
// same functions so stored and runtime normalisation can never disagree.
// ---------------------------------------------------------------------------
class AmelTechText {
public:
    // Lowercase, strip punctuation, expand abbreviations, collapse spaces.
    static void normalize(const char* in, char* out, size_t outSize);

    // Split a normalized string on single spaces.
    static int tokenize(const char* text,
                        char tokens[][AMELTECH_MAX_TOKEN_LEN],
                        int maxTokens);

    static uint32_t fnv1a(const char* s, uint32_t seed = 2166136261UL);
    static uint32_t fnv1aLen(const char* s, size_t len, uint32_t seed = 2166136261UL);

    // 32-bit presence sketch over content tokens.
    static uint32_t tokenSignature(const char* normalized);

    // 64-bit character-trigram sketch; survives typos and word order changes.
    static uint64_t trigramBloom(const char* normalized);

    static bool isStopWord(const char* token);
    static bool isQuestionWord(const char* token);

    // Information weight of a token: rare/long words count more than filler.
    static float tokenWeight(const char* token);

    static uint8_t popcount32(uint32_t v);
    static uint8_t popcount64(uint64_t v);

    // Bounded Levenshtein. Returns maxDist+1 when the bound is exceeded.
    static int levenshtein(const char* a, const char* b, int maxDist);
    static float editSimilarity(const char* a, const char* b);

    static void copyTrimmed(const char* in, char* out, size_t outSize);
};

// ---------------------------------------------------------------------------
// Precomputed features of the question being asked. Built once per ask().
// ---------------------------------------------------------------------------
struct AmelTechQuery {
    char normalized[AMELTECH_MAX_QUESTION_LEN];
    char tokens[AMELTECH_MAX_TOKENS][AMELTECH_MAX_TOKEN_LEN];
    float weights[AMELTECH_MAX_TOKENS];
    int tokenCount;
    float totalWeight;
    uint32_t signature;
    uint64_t bloom;
    float vector[AMELTECH_NEURAL_DIM];
    uint16_t length;
    bool valid;
};

// ---------------------------------------------------------------------------
// Similarity scorer
// ---------------------------------------------------------------------------
class NeuralEngine {
public:
    // Build the feature bundle for a question.
    static void buildQuery(const char* rawQuestion, AmelTechQuery& out);
    static void buildQueryFromNormalized(const char* normalized, AmelTechQuery& out);

    // Fill a unit-length hashed feature vector.
    static void embed(const char* normalized, float* vector);
    static void embedTokens(const char tokens[][AMELTECH_MAX_TOKEN_LEN],
                            const float* weights, int count, float* vector);

    static float cosine(const float* a, const float* b);

    // Stage 1: O(1) per candidate, uses only the precomputed sketches.
    static float prefilterScore(const AmelTechQuery& q,
                                uint32_t candSignature,
                                uint64_t candBloom,
                                uint8_t candTokenCount);

    // Stage 2: full blended similarity against a candidate's normalized text.
    static float fullScore(const AmelTechQuery& q,
                           const char* candNormalized,
                           uint32_t candSignature,
                           uint64_t candBloom);

    // Weighted token F-measure between the query and a candidate.
    static float tokenOverlap(const AmelTechQuery& q, const char* candNormalized);

    static float trigramDice(uint64_t a, uint64_t b);
    static float signatureOverlap(uint32_t a, uint32_t b);

    // Confidence calibration: maps a raw blended score onto the published
    // confidence bands so thresholds mean the same thing everywhere.
    static float calibrate(float rawScore, bool exactMatch);
};

#endif // AMELTECH_NEURAL_ENGINE_H
