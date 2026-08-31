#include "NeuralEngine.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

// Scratch space for a single word while normalising. Long enough for any real
// word plus the longest replacement in the tables below.
#define AMEL_TOKEN_SCRATCH 32

// ---------------------------------------------------------------------------
// Abbreviation / synonym table.
//
// IMPORTANT: tools/generate_knowledge.py contains a byte-identical copy of this
// table. Stored normalized text and runtime normalized text MUST agree or exact
// matching silently degrades to fuzzy matching. (That was a real defect in the
// previous release: "wifi" normalized to "wi fi" at runtime but was stored as
// "wifi", so every Wi-Fi question lost its exact match.)
// ---------------------------------------------------------------------------
struct AmelSynPair {
    const char* from;
    const char* to;
};

static const AmelSynPair AMEL_SYN_TABLE[] = {
    // time
    {"sec", "seconds"}, {"secs", "seconds"}, {"second", "seconds"},
    {"min", "minute"},  {"mins", "minutes"},
    {"hr", "hour"},     {"hrs", "hours"},
    // chat shorthand
    {"r", "are"}, {"u", "you"}, {"ur", "your"}, {"y", "why"},
    {"whats", "what is"}, {"wats", "what is"}, {"wat", "what"}, {"wht", "what"},
    {"hows", "how is"}, {"whos", "who is"}, {"wheres", "where is"},
    {"whens", "when is"}, {"hw", "how"}, {"hru", "how are you"},
    {"pls", "please"}, {"plz", "please"}, {"thx", "thanks"}, {"ty", "thanks"},
    {"im", "i am"}, {"ive", "i have"}, {"dont", "do not"}, {"cant", "can not"},
    {"wont", "will not"}, {"gimme", "give me"}, {"lemme", "let me"},
    // technical shorthand
    {"temp", "temperature"}, {"humid", "humidity"},
    {"freq", "frequency"}, {"mem", "memory"},
    {"cfg", "configuration"}, {"config", "configuration"},
    {"info", "information"}, {"docs", "documentation"},
    {"mcu", "microcontroller"}, {"mic", "microcontroller"},
    {"bt", "bluetooth"}, {"volt", "voltage"}, {"amp", "ampere"},
    // small numbers
    {"1", "one"}, {"2", "two"}, {"3", "three"}, {"4", "four"},
    {"5", "five"}, {"6", "six"}, {"7", "seven"}, {"8", "eight"},
    {"9", "nine"}, {"10", "ten"}, {"60", "sixty"},
    {nullptr, nullptr}
};

// Phrase-level repairs applied after token expansion.
static const AmelSynPair AMEL_PHRASE_TABLE[] = {
    {"wi fi", "wifi"},
    {"e mail", "email"},
    {"micro controller", "microcontroller"},
    {"esp 32", "esp32"},
    {"data sheet", "datasheet"},
    {"blue tooth", "bluetooth"},
    {nullptr, nullptr}
};

static const char* const AMEL_STOP_WORDS[] = {
    "the", "a", "an", "is", "are", "was", "were", "be", "been", "am",
    "of", "in", "on", "at", "to", "for", "and", "or", "it", "its",
    "this", "that", "these", "those", "do", "does", "did", "with",
    "as", "by", "from", "please", "tell", "hey", "so", "there", "here",
    nullptr
};

static const char* const AMEL_QUESTION_WORDS[] = {
    "what", "how", "why", "when", "where", "who", "which", "whose", "whom",
    nullptr
};

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------
uint32_t AmelTechText::fnv1aLen(const char* s, size_t len, uint32_t seed) {
    uint32_t h = seed;
    if (!s) return h;
    for (size_t i = 0; i < len; ++i) {
        h ^= (uint8_t)s[i];
        h *= 16777619UL;
    }
    return h;
}

uint32_t AmelTechText::fnv1a(const char* s, uint32_t seed) {
    if (!s) return seed;
    return fnv1aLen(s, strlen(s), seed);
}

uint8_t AmelTechText::popcount32(uint32_t v) {
#if defined(__GNUC__)
    return (uint8_t)__builtin_popcount(v);
#else
    uint8_t c = 0;
    while (v) { v &= (v - 1); ++c; }
    return c;
#endif
}

uint8_t AmelTechText::popcount64(uint64_t v) {
#if defined(__GNUC__)
    return (uint8_t)__builtin_popcountll(v);
#else
    uint8_t c = 0;
    while (v) { v &= (v - 1); ++c; }
    return c;
#endif
}

// ---------------------------------------------------------------------------
// Word classes
// ---------------------------------------------------------------------------
bool AmelTechText::isStopWord(const char* token) {
    if (!token || !token[0]) return true;
    for (int i = 0; AMEL_STOP_WORDS[i]; ++i) {
        if (strcmp(token, AMEL_STOP_WORDS[i]) == 0) return true;
    }
    return false;
}

bool AmelTechText::isQuestionWord(const char* token) {
    if (!token || !token[0]) return false;
    for (int i = 0; AMEL_QUESTION_WORDS[i]; ++i) {
        if (strcmp(token, AMEL_QUESTION_WORDS[i]) == 0) return true;
    }
    return false;
}

float AmelTechText::tokenWeight(const char* token) {
    if (!token || !token[0]) return 0.0f;
    size_t len = strlen(token);
    if (isStopWord(token)) return 0.30f;
    if (isQuestionWord(token)) return 0.55f;
    if (len <= 2) return 0.55f;
    if (len <= 4) return 0.95f;
    if (len <= 6) return 1.05f;
    return 1.20f;
}

// ---------------------------------------------------------------------------
// Normalisation
// ---------------------------------------------------------------------------
static void amelExpandToken(const char* tok, char* out, size_t outSize) {
    for (int i = 0; AMEL_SYN_TABLE[i].from; ++i) {
        if (strcmp(tok, AMEL_SYN_TABLE[i].from) == 0) {
            strncpy(out, AMEL_SYN_TABLE[i].to, outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
    }
    strncpy(out, tok, outSize - 1);
    out[outSize - 1] = '\0';
}

// Replace every occurrence of `from` with `to`, honouring word boundaries.
static void amelReplacePhrase(char* buf, size_t bufSize, const char* from, const char* to) {
    size_t fl = strlen(from);
    size_t tl = strlen(to);
    if (fl == 0) return;
    char* p = buf;
    while ((p = strstr(p, from)) != nullptr) {
        bool leftOk = (p == buf) || (*(p - 1) == ' ');
        char after = *(p + fl);
        bool rightOk = (after == '\0' || after == ' ');
        if (!leftOk || !rightOk) {
            p += 1;
            continue;
        }
        size_t tailLen = strlen(p + fl);
        size_t headLen = (size_t)(p - buf);
        if (headLen + tl + tailLen >= bufSize) return;  // refuse to overflow
        memmove(p + tl, p + fl, tailLen + 1);
        memcpy(p, to, tl);
        p += tl;
    }
}

void AmelTechText::normalize(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t j = 0;
    char token[AMEL_TOKEN_SCRATCH];
    size_t ti = 0;
    char expanded[AMEL_TOKEN_SCRATCH + 16];

    for (const char* p = in;; ++p) {
        char c = *p;
        bool end = (c == '\0');
        if (!end && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

        // Apostrophes are removed, not turned into separators, so "ohm's law"
        // and "ohms law" normalize identically and "what's" can expand to
        // "what is". A curly UTF-8 apostrophe is treated the same way.
        if (!end && c == '\'') continue;
        if (!end && (unsigned char)c == 0xE2 &&
            (unsigned char)p[1] == 0x80 &&
            ((unsigned char)p[2] == 0x99 || (unsigned char)p[2] == 0x98)) {
            p += 2;
            continue;
        }

        if (!end && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            if (ti < sizeof(token) - 1) token[ti++] = c;
            continue;
        }

        // Separator or end of input: flush the pending token.
        if (ti > 0) {
            token[ti] = '\0';
            ti = 0;
            amelExpandToken(token, expanded, sizeof(expanded));
            size_t el = strlen(expanded);
            if (j > 0) {
                if (j + 1 >= outSize) break;
                out[j++] = ' ';
            }
            if (j + el >= outSize) {
                el = (outSize - 1) - j;   // truncate rather than overflow
            }
            memcpy(out + j, expanded, el);
            j += el;
        }
        if (end || j + 1 >= outSize) break;
    }
    out[j] = '\0';

    for (int i = 0; AMEL_PHRASE_TABLE[i].from; ++i) {
        amelReplacePhrase(out, outSize, AMEL_PHRASE_TABLE[i].from, AMEL_PHRASE_TABLE[i].to);
    }
}

int AmelTechText::tokenize(const char* text,
                           char tokens[][AMELTECH_MAX_TOKEN_LEN],
                           int maxTokens) {
    if (!text || maxTokens <= 0) return 0;
    int n = 0;
    const char* p = text;
    while (*p && n < maxTokens) {
        while (*p == ' ') ++p;
        if (!*p) break;
        int i = 0;
        while (*p && *p != ' ') {
            if (i < AMELTECH_MAX_TOKEN_LEN - 1) tokens[n][i++] = *p;
            ++p;
        }
        tokens[n][i] = '\0';
        if (i > 0) ++n;
    }
    return n;
}

uint32_t AmelTechText::tokenSignature(const char* normalized) {
    if (!normalized || !normalized[0]) return 0;
    uint32_t sig = 0;
    char tok[AMELTECH_MAX_TOKEN_LEN];
    size_t ti = 0;
    for (const char* p = normalized;; ++p) {
        if (*p && *p != ' ') {
            if (ti < sizeof(tok) - 1) tok[ti++] = *p;
            continue;
        }
        if (ti > 0) {
            tok[ti] = '\0';
            ti = 0;
            if (!isStopWord(tok)) {
                sig |= (1UL << (fnv1a(tok) % 32u));
            }
        }
        if (!*p) break;
    }
    return sig;
}

uint64_t AmelTechText::trigramBloom(const char* normalized) {
    if (!normalized || !normalized[0]) return 0;
    size_t len = strlen(normalized);
    char padded[AMELTECH_MAX_QUESTION_LEN + 4];
    size_t copy = len;
    if (copy > sizeof(padded) - 3) copy = sizeof(padded) - 3;
    padded[0] = ' ';
    memcpy(padded + 1, normalized, copy);
    padded[copy + 1] = ' ';
    padded[copy + 2] = '\0';

    size_t plen = copy + 2;
    uint64_t bloom = 0;
    if (plen < 3) {
        bloom |= (1ULL << (fnv1aLen(padded, plen) % 64u));
        return bloom;
    }
    for (size_t i = 0; i + 3 <= plen; ++i) {
        uint32_t h = fnv1aLen(padded + i, 3);
        bloom |= (1ULL << (h % 64u));
    }
    return bloom;
}

int AmelTechText::levenshtein(const char* a, const char* b, int maxDist) {
    if (!a || !b) return maxDist + 1;
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (maxDist < 0) maxDist = 0;
    if (abs(la - lb) > maxDist) return maxDist + 1;
    if (la == 0) return lb;
    if (lb == 0) return la;

    const int CAP = AMELTECH_MAX_QUESTION_LEN;
    if (la > CAP) la = CAP;
    if (lb > CAP) lb = CAP;

    // Values never exceed CAP (128), so a byte per cell is enough.
    uint8_t prev[AMELTECH_MAX_QUESTION_LEN + 1];
    uint8_t curr[AMELTECH_MAX_QUESTION_LEN + 1];

    for (int j = 0; j <= lb; ++j) prev[j] = (uint8_t)j;

    for (int i = 1; i <= la; ++i) {
        curr[0] = (uint8_t)i;
        int rowMin = i;
        for (int j = 1; j <= lb; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int v = del < ins ? del : ins;
            if (sub < v) v = sub;
            curr[j] = (uint8_t)v;
            if (v < rowMin) rowMin = v;
        }
        if (rowMin > maxDist) return maxDist + 1;
        memcpy(prev, curr, (size_t)lb + 1);
    }
    return prev[lb];
}

float AmelTechText::editSimilarity(const char* a, const char* b) {
    if (!a || !b) return 0.0f;
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (la == 0 && lb == 0) return 1.0f;
    int maxLen = la > lb ? la : lb;
    if (maxLen == 0) return 1.0f;
    int maxDist = maxLen / 3 + 2;
    if (maxDist > 16) maxDist = 16;
    int d = levenshtein(a, b, maxDist);
    if (d > maxDist) return 0.0f;
    float s = 1.0f - (float)d / (float)maxLen;
    return s < 0.0f ? 0.0f : s;
}

void AmelTechText::copyTrimmed(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in) return;
    while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') ++in;
    size_t len = strlen(in);
    while (len > 0) {
        char c = in[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') --len;
        else break;
    }
    if (len >= outSize) len = outSize - 1;
    memcpy(out, in, len);
    out[len] = '\0';
}

// ---------------------------------------------------------------------------
// Embedding
// ---------------------------------------------------------------------------
static const uint32_t AMEL_PROJ_SEEDS[3] = {
    2166136261UL, 2654435761UL, 40503UL
};

void NeuralEngine::embedTokens(const char tokens[][AMELTECH_MAX_TOKEN_LEN],
                               const float* weights, int count, float* vector) {
    for (int d = 0; d < AMELTECH_NEURAL_DIM; ++d) vector[d] = 0.0f;
    if (count <= 0) return;

    for (int i = 0; i < count; ++i) {
        float w = weights ? weights[i] : 1.0f;
        if (w <= 0.0f) continue;
        for (int k = 0; k < 3; ++k) {
            uint32_t h = AmelTechText::fnv1a(tokens[i], AMEL_PROJ_SEEDS[k]);
            int idx = (int)(h % (uint32_t)AMELTECH_NEURAL_DIM);
            float sign = ((h >> 31) & 1u) ? -1.0f : 1.0f;
            vector[idx] += sign * w;
        }
    }

    float norm = 0.0f;
    for (int d = 0; d < AMELTECH_NEURAL_DIM; ++d) norm += vector[d] * vector[d];
    if (norm <= 1e-9f) return;
    norm = sqrtf(norm);
    for (int d = 0; d < AMELTECH_NEURAL_DIM; ++d) vector[d] /= norm;
}

void NeuralEngine::embed(const char* normalized, float* vector) {
    char tokens[AMELTECH_MAX_TOKENS][AMELTECH_MAX_TOKEN_LEN];
    float weights[AMELTECH_MAX_TOKENS];
    int n = AmelTechText::tokenize(normalized, tokens, AMELTECH_MAX_TOKENS);
    for (int i = 0; i < n; ++i) weights[i] = AmelTechText::tokenWeight(tokens[i]);
    embedTokens(tokens, weights, n, vector);
}

float NeuralEngine::cosine(const float* a, const float* b) {
    if (!a || !b) return 0.0f;
    float dot = 0.0f;
    for (int d = 0; d < AMELTECH_NEURAL_DIM; ++d) dot += a[d] * b[d];
    if (dot < 0.0f) dot = 0.0f;
    if (dot > 1.0f) dot = 1.0f;
    return dot;
}

// ---------------------------------------------------------------------------
// Query construction
// ---------------------------------------------------------------------------
void NeuralEngine::buildQueryFromNormalized(const char* normalized, AmelTechQuery& out) {
    memset(&out, 0, sizeof(out));
    if (!normalized || !normalized[0]) {
        out.valid = false;
        return;
    }
    strncpy(out.normalized, normalized, sizeof(out.normalized) - 1);
    out.normalized[sizeof(out.normalized) - 1] = '\0';
    out.length = (uint16_t)strlen(out.normalized);

    out.tokenCount = AmelTechText::tokenize(out.normalized, out.tokens, AMELTECH_MAX_TOKENS);
    out.totalWeight = 0.0f;
    for (int i = 0; i < out.tokenCount; ++i) {
        out.weights[i] = AmelTechText::tokenWeight(out.tokens[i]);
        out.totalWeight += out.weights[i];
    }
    out.signature = AmelTechText::tokenSignature(out.normalized);
    out.bloom = AmelTechText::trigramBloom(out.normalized);
    embedTokens(out.tokens, out.weights, out.tokenCount, out.vector);
    out.valid = (out.tokenCount > 0);
}

void NeuralEngine::buildQuery(const char* rawQuestion, AmelTechQuery& out) {
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(rawQuestion, norm, sizeof(norm));
    buildQueryFromNormalized(norm, out);
}

// ---------------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------------
float NeuralEngine::signatureOverlap(uint32_t a, uint32_t b) {
    uint32_t inter = a & b;
    uint32_t uni = a | b;
    uint8_t u = AmelTechText::popcount32(uni);
    if (u == 0) return 0.0f;
    return (float)AmelTechText::popcount32(inter) / (float)u;
}

float NeuralEngine::trigramDice(uint64_t a, uint64_t b) {
    uint8_t pa = AmelTechText::popcount64(a);
    uint8_t pb = AmelTechText::popcount64(b);
    if (pa == 0 || pb == 0) return 0.0f;
    uint8_t inter = AmelTechText::popcount64(a & b);
    return (2.0f * (float)inter) / (float)(pa + pb);
}

float NeuralEngine::prefilterScore(const AmelTechQuery& q,
                                   uint32_t candSignature,
                                   uint64_t candBloom,
                                   uint8_t candTokenCount) {
    float sig = signatureOverlap(q.signature, candSignature);
    float dice = trigramDice(q.bloom, candBloom);

    int nq = q.tokenCount;
    int nc = candTokenCount ? candTokenCount : nq;
    int hi = nq > nc ? nq : nc;
    float lenSim = 1.0f;
    if (hi > 0) {
        int diff = nq > nc ? (nq - nc) : (nc - nq);
        lenSim = 1.0f - (float)diff / (float)hi;
        if (lenSim < 0.0f) lenSim = 0.0f;
    }
    return 0.45f * sig + 0.45f * dice + 0.10f * lenSim;
}

float NeuralEngine::tokenOverlap(const AmelTechQuery& q, const char* candNormalized) {
    if (q.tokenCount <= 0 || !candNormalized || !candNormalized[0]) return 0.0f;

    char candTokens[AMELTECH_MAX_TOKENS][AMELTECH_MAX_TOKEN_LEN];
    int nc = AmelTechText::tokenize(candNormalized, candTokens, AMELTECH_MAX_TOKENS);
    if (nc <= 0) return 0.0f;

    bool candUsed[AMELTECH_MAX_TOKENS];
    for (int j = 0; j < nc; ++j) candUsed[j] = false;

    float matchedQueryWeight = 0.0f;
    float candWeightTotal = 0.0f;
    float candWeightMatched = 0.0f;

    for (int j = 0; j < nc; ++j) candWeightTotal += AmelTechText::tokenWeight(candTokens[j]);
    if (candWeightTotal <= 0.0f || q.totalWeight <= 0.0f) return 0.0f;

    for (int i = 0; i < q.tokenCount; ++i) {
        int bestJ = -1;
        float bestSim = 0.0f;
        for (int j = 0; j < nc; ++j) {
            if (candUsed[j]) continue;
            if (strcmp(q.tokens[i], candTokens[j]) == 0) {
                bestJ = j;
                bestSim = 1.0f;
                break;
            }
            // Tolerate a single typo inside a reasonably long word.
            size_t la = strlen(q.tokens[i]);
            size_t lb = strlen(candTokens[j]);
            if (la >= 5 && lb >= 5) {
                size_t diff = la > lb ? la - lb : lb - la;
                if (diff <= 2) {
                    int d = AmelTechText::levenshtein(q.tokens[i], candTokens[j], 1);
                    if (d <= 1) {
                        float sim = 0.85f;
                        if (sim > bestSim) { bestSim = sim; bestJ = j; }
                    }
                }
            }
        }
        if (bestJ >= 0) {
            candUsed[bestJ] = true;
            matchedQueryWeight += q.weights[i] * bestSim;
            candWeightMatched += AmelTechText::tokenWeight(candTokens[bestJ]) * bestSim;
        }
    }

    float precision = matchedQueryWeight / q.totalWeight;
    float recall = candWeightMatched / candWeightTotal;
    if (precision <= 0.0f || recall <= 0.0f) return 0.0f;
    return (2.0f * precision * recall) / (precision + recall);
}

float NeuralEngine::fullScore(const AmelTechQuery& q,
                              const char* candNormalized,
                              uint32_t candSignature,
                              uint64_t candBloom) {
    if (!q.valid || !candNormalized || !candNormalized[0]) return 0.0f;
    if (strcmp(q.normalized, candNormalized) == 0) return 1.0f;

    float overlap = tokenOverlap(q, candNormalized);
    float dice = trigramDice(q.bloom, candBloom);
    float edit = AmelTechText::editSimilarity(q.normalized, candNormalized);
    float sig = signatureOverlap(q.signature, candSignature);

    float candVec[AMELTECH_NEURAL_DIM];
    embed(candNormalized, candVec);
    float cos = cosine(q.vector, candVec);

    // Five independent views of similarity. Weighting several weak signals
    // beats trusting any single one: token overlap catches paraphrases, the
    // hashed vector catches word-order changes, trigrams and edit distance
    // catch typos, and the signature catches shared rare terms.
    float raw = 0.34f * overlap + 0.24f * cos + 0.18f * dice +
                0.16f * edit + 0.08f * sig;

    // One phrase fully contained in the other is strong evidence.
    if (strstr(candNormalized, q.normalized) || strstr(q.normalized, candNormalized)) {
        raw += 0.07f;
    }

    // Agreement on the interrogative ("what" vs "how") matters a lot for
    // picking the right answer among otherwise similar questions.
    const char* qWord = nullptr;
    for (int i = 0; i < q.tokenCount; ++i) {
        if (AmelTechText::isQuestionWord(q.tokens[i])) { qWord = q.tokens[i]; break; }
    }
    if (qWord) {
        char candTok[AMELTECH_MAX_TOKENS][AMELTECH_MAX_TOKEN_LEN];
        int nc = AmelTechText::tokenize(candNormalized, candTok, AMELTECH_MAX_TOKENS);
        const char* cWord = nullptr;
        for (int j = 0; j < nc; ++j) {
            if (AmelTechText::isQuestionWord(candTok[j])) { cWord = candTok[j]; break; }
        }
        if (cWord) {
            if (strcmp(qWord, cWord) == 0) raw += 0.03f;
            else raw -= 0.06f;
        }
    }

    if (raw < 0.0f) raw = 0.0f;
    if (raw > 1.0f) raw = 1.0f;
    return raw;
}

float NeuralEngine::calibrate(float rawScore, bool exactMatch) {
    if (exactMatch) return 1.0f;
    float r = rawScore;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;

    float c;
    if (r >= 0.85f)      c = 0.90f + (r - 0.85f) / 0.15f * 0.10f;
    else if (r >= 0.68f) c = 0.75f + (r - 0.68f) / 0.17f * 0.15f;
    else if (r >= 0.50f) c = 0.52f + (r - 0.50f) / 0.18f * 0.23f;
    else                 c = r * 1.04f;

    if (c > 1.0f) c = 1.0f;
    if (c < 0.0f) c = 0.0f;
    return c;
}
