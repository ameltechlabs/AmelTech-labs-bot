#include "KnowledgeBase.h"
#include <string.h>
#include <ctype.h>
#include <math.h>

#if defined(ESP32)
#include <Preferences.h>
#endif

// ---------------------------------------------------------------------------
// Simple synonym / abbreviation table (static, flash-friendly)
// ---------------------------------------------------------------------------
struct SynPair {
    const char* from;
    const char* to;
};

static const SynPair SYN_TABLE[] = {
    {"sec", "seconds"}, {"secs", "seconds"}, {"second", "seconds"},
    {"min", "minute"}, {"mins", "minutes"},
    {"hr", "hour"}, {"hrs", "hours"},
    {"r", "are"}, {"u", "you"}, {"ur", "your"},
    {"whats", "what is"}, {"wht", "what"}, {"hw", "how"},
    {"pls", "please"}, {"thx", "thanks"}, {"temp", "temperature"},
    {"freq", "frequency"}, {"mem", "memory"}, {"cfg", "config"},
    {"wifi", "wi fi"}, {"bt", "bluetooth"}, {"mcu", "microcontroller"},
    {"mic", "microcontroller"}, {"rssi", "signal strength"},
    {"1", "one"}, {"2", "two"}, {"3", "three"}, {"4", "four"},
    {"5", "five"}, {"6", "six"}, {"7", "seven"}, {"8", "eight"},
    {"9", "nine"}, {"10", "ten"}, {"60", "sixty"},
    {nullptr, nullptr}
};

static void expandToken(const char* tok, char* out, size_t outSize) {
    for (int i = 0; SYN_TABLE[i].from; ++i) {
        if (strcmp(tok, SYN_TABLE[i].from) == 0) {
            strncpy(out, SYN_TABLE[i].to, outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
    }
    strncpy(out, tok, outSize - 1);
    out[outSize - 1] = '\0';
}

// ---------------------------------------------------------------------------
KnowledgeBase::KnowledgeBase() : _userCount(0) {
    memset(_user, 0, sizeof(_user));
}

bool KnowledgeBase::begin() {
    _userCount = 0;
    memset(_user, 0, sizeof(_user));
    loadFromNvs();
    return true;
}

void KnowledgeBase::clearUser() {
    memset(_user, 0, sizeof(_user));
    _userCount = 0;
}

size_t KnowledgeBase::userCount() const {
    size_t n = 0;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (_user[i].used) ++n;
    }
    return n;
}

// ---------------------------------------------------------------------------
void KnowledgeBase::normalize(const char* in, char* out, size_t outSize) {
    if (!in || !out || outSize == 0) {
        if (out && outSize) out[0] = '\0';
        return;
    }
    size_t j = 0;
    bool space = true;
    char token[32];
    size_t ti = 0;

    auto flushToken = [&]() {
        if (ti == 0) return;
        token[ti] = '\0';
        char expanded[40];
        expandToken(token, expanded, sizeof(expanded));
        size_t el = strlen(expanded);
        if (j > 0 && j < outSize - 1) {
            out[j++] = ' ';
        }
        for (size_t k = 0; k < el && j < outSize - 1; ++k) {
            out[j++] = expanded[k];
        }
        ti = 0;
    };

    for (const char* p = in; *p && j < outSize - 1; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (ti < sizeof(token) - 1) token[ti++] = c;
            space = false;
        } else {
            // punctuation / whitespace → token boundary
            flushToken();
            space = true;
        }
    }
    flushToken();
    out[j] = '\0';
    // collapse any accidental double spaces already avoided
}

int KnowledgeBase::tokenize(const char* text, char tokens[][32], int maxTokens) {
    if (!text || maxTokens <= 0) return 0;
    int n = 0;
    const char* p = text;
    while (*p && n < maxTokens) {
        while (*p == ' ') ++p;
        if (!*p) break;
        int i = 0;
        while (*p && *p != ' ' && i < 31) {
            tokens[n][i++] = *p++;
        }
        tokens[n][i] = '\0';
        if (i > 0) ++n;
    }
    return n;
}

float KnowledgeBase::keywordScore(const char* tokens[], int nTokens, const char* candidateNorm) {
    if (nTokens <= 0 || !candidateNorm) return 0.0f;
    char candTokens[AMELTECH_MAX_TOKENS][32];
    int cn = tokenize(candidateNorm, candTokens, AMELTECH_MAX_TOKENS);
    if (cn <= 0) return 0.0f;

    int hits = 0;
    for (int i = 0; i < nTokens; ++i) {
        if (!tokens[i] || !tokens[i][0]) continue;
        // skip very common stop words for scoring
        if (strcmp(tokens[i], "the") == 0 || strcmp(tokens[i], "a") == 0 ||
            strcmp(tokens[i], "an") == 0 || strcmp(tokens[i], "is") == 0 ||
            strcmp(tokens[i], "of") == 0 || strcmp(tokens[i], "in") == 0 ||
            strcmp(tokens[i], "to") == 0 || strcmp(tokens[i], "and") == 0) {
            continue;
        }
        for (int j = 0; j < cn; ++j) {
            if (strcmp(tokens[i], candTokens[j]) == 0) {
                ++hits;
                break;
            }
        }
    }
    // meaningful token count
    int meaningful = 0;
    for (int i = 0; i < nTokens; ++i) {
        if (!tokens[i] || !tokens[i][0]) continue;
        if (strcmp(tokens[i], "the") == 0 || strcmp(tokens[i], "a") == 0 ||
            strcmp(tokens[i], "an") == 0 || strcmp(tokens[i], "is") == 0 ||
            strcmp(tokens[i], "of") == 0 || strcmp(tokens[i], "in") == 0 ||
            strcmp(tokens[i], "to") == 0 || strcmp(tokens[i], "and") == 0) {
            continue;
        }
        ++meaningful;
    }
    if (meaningful == 0) return 0.0f;
    return (float)hits / (float)meaningful;
}

int KnowledgeBase::levenshtein(const char* a, const char* b, int maxDist) {
    // Bounded Levenshtein (Wagner-Fischer with early exit)
    if (!a || !b) return maxDist + 1;
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (abs(la - lb) > maxDist) return maxDist + 1;
    if (la == 0) return lb;
    if (lb == 0) return la;
    if (la > 64 || lb > 64) return maxDist + 1;  // safety

    // Use two rows
    int prev[65], curr[65];
    for (int j = 0; j <= lb; ++j) prev[j] = j;

    for (int i = 1; i <= la; ++i) {
        curr[0] = i;
        int rowMin = curr[0];
        for (int j = 1; j <= lb; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int v = del < ins ? del : ins;
            if (sub < v) v = sub;
            curr[j] = v;
            if (v < rowMin) rowMin = v;
        }
        if (rowMin > maxDist) return maxDist + 1;
        for (int j = 0; j <= lb; ++j) prev[j] = curr[j];
    }
    return prev[lb];
}

float KnowledgeBase::fuzzyScore(const char* a, const char* b) {
    if (!a || !b) return 0.0f;
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    if (la == 0 && lb == 0) return 1.0f;
    int maxLen = la > lb ? la : lb;
    if (maxLen == 0) return 1.0f;
    int maxDist = maxLen / 3 + 2;  // allow ~33% edits
    if (maxDist > 12) maxDist = 12;
    int dist = levenshtein(a, b, maxDist);
    if (dist > maxDist) return 0.0f;
    float score = 1.0f - (float)dist / (float)maxLen;
    if (score < 0.0f) score = 0.0f;
    return score;
}

float KnowledgeBase::lightweightSimilarity(const char* a, const char* b) {
    // Combination of token overlap + fuzzy on whole string
    if (!a || !b) return 0.0f;
    if (strcmp(a, b) == 0) return 1.0f;

    char tokA[AMELTECH_MAX_TOKENS][32];
    char tokB[AMELTECH_MAX_TOKENS][32];
    int na = tokenize(a, tokA, AMELTECH_MAX_TOKENS);
    int nb = tokenize(b, tokB, AMELTECH_MAX_TOKENS);
    if (na == 0 || nb == 0) return fuzzyScore(a, b);

    const char* ptrs[AMELTECH_MAX_TOKENS];
    for (int i = 0; i < na; ++i) ptrs[i] = tokA[i];
    float kw = keywordScore(ptrs, na, b);

    float fz = fuzzyScore(a, b);
    // weighted blend
    float sim = 0.55f * kw + 0.45f * fz;
    if (sim > 1.0f) sim = 1.0f;
    return sim;
}

float KnowledgeBase::scoreCandidate(const char* norm, const char* candNorm, const char* candQ) {
    if (!norm || !candNorm) return 0.0f;

    // Exact normalized match
    if (strcmp(norm, candNorm) == 0) return 1.0f;

    // Exact original-ish (already normalized)
    float fz = fuzzyScore(norm, candNorm);
    if (fz >= 0.92f) return fz;

    char tokens[AMELTECH_MAX_TOKENS][32];
    int nt = tokenize(norm, tokens, AMELTECH_MAX_TOKENS);
    const char* ptrs[AMELTECH_MAX_TOKENS];
    for (int i = 0; i < nt; ++i) ptrs[i] = tokens[i];

    float kw = keywordScore(ptrs, nt, candNorm);
    float sim = lightweightSimilarity(norm, candNorm);

    // Prefer candidates that share key content words
    float score = 0.40f * kw + 0.35f * sim + 0.25f * fz;

    // Small boost if candidate question is substring-ish
    if (strstr(candNorm, norm) || strstr(norm, candNorm)) {
        score += 0.08f;
    }
    if (score > 1.0f) score = 1.0f;
    return score;
}

// ---------------------------------------------------------------------------
MatchResult KnowledgeBase::matchBuiltin(const char* norm, const char* /*original*/) {
    MatchResult best;
    memset(&best, 0, sizeof(best));
    best.confidence = 0.0f;
    best.found = false;
    size_t bestIdx = 0;

    for (size_t i = 0; i < AMELTECH_BUILTIN_KNOWLEDGE_COUNT; ++i) {
#if defined(ESP32)
        // On ESP32, string literals in the generated table reside in flash.
        // Pointers remain valid for String() construction which copies as needed.
        AmelTechBuiltinEntry entry;
        memcpy_P(&entry, &AMELTECH_BUILTIN_KNOWLEDGE[i], sizeof(AmelTechBuiltinEntry));
        char nbuf[AMELTECH_MAX_QUESTION_LEN];
        char qbuf[AMELTECH_MAX_QUESTION_LEN];
        strncpy_P(nbuf, (PGM_P)entry.normalized, sizeof(nbuf) - 1);
        nbuf[sizeof(nbuf) - 1] = '\0';
        strncpy_P(qbuf, (PGM_P)entry.question, sizeof(qbuf) - 1);
        qbuf[sizeof(qbuf) - 1] = '\0';
        float sc = scoreCandidate(norm, nbuf, qbuf);
#else
        const AmelTechBuiltinEntry& e = AMELTECH_BUILTIN_KNOWLEDGE[i];
        float sc = scoreCandidate(norm, e.normalized, e.question);
#endif
        if (sc > best.confidence) {
            best.confidence = sc;
            best.found = (sc >= 0.50f);
            bestIdx = i;
            best.fromUser = false;
        }
    }

    if (best.found) {
#if defined(ESP32)
        AmelTechBuiltinEntry entry;
        memcpy_P(&entry, &AMELTECH_BUILTIN_KNOWLEDGE[bestIdx], sizeof(AmelTechBuiltinEntry));
        best.answer = entry.answer;
        best.category = entry.category;
        best.matchedQuestion = entry.question;
#else
        const AmelTechBuiltinEntry& e = AMELTECH_BUILTIN_KNOWLEDGE[bestIdx];
        best.answer = e.answer;
        best.category = e.category;
        best.matchedQuestion = e.question;
#endif
    }
    return best;
}

MatchResult KnowledgeBase::matchUser(const char* norm, const char* /*original*/) {
    MatchResult best;
    memset(&best, 0, sizeof(best));
    best.confidence = 0.0f;
    best.found = false;

    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i].used) continue;
        float sc = scoreCandidate(norm, _user[i].normalized, _user[i].question);
        if (sc > best.confidence) {
            best.confidence = sc;
            best.found = (sc >= 0.50f);
            best.answer = _user[i].answer;
            best.category = _user[i].category;
            best.matchedQuestion = _user[i].question;
            best.fromUser = true;
        }
    }
    return best;
}

MatchResult KnowledgeBase::findBest(const char* question, float minConfidence) {
    MatchResult none;
    memset(&none, 0, sizeof(none));
    none.found = false;
    none.confidence = 0.0f;

    if (!question || !question[0]) return none;

    char norm[AMELTECH_MAX_QUESTION_LEN];
    normalize(question, norm, sizeof(norm));
    if (!norm[0]) return none;

    MatchResult u = matchUser(norm, question);
    MatchResult b = matchBuiltin(norm, question);

    MatchResult best = u;
    if (b.confidence > best.confidence) best = b;

    if (best.confidence < minConfidence) {
        best.found = false;
    }
    return best;
}

// ---------------------------------------------------------------------------
bool KnowledgeBase::answersConflict(const char* a, const char* b) const {
    if (!a || !b) return false;
    // Simple heuristic: if normalized answers differ significantly
    char na[AMELTECH_MAX_ANSWER_LEN];
    char nb[AMELTECH_MAX_ANSWER_LEN];
    normalize(a, na, sizeof(na));
    normalize(b, nb, sizeof(nb));
    if (strcmp(na, nb) == 0) return false;
    float sim = lightweightSimilarity(na, nb);
    return sim < 0.55f;
}

int8_t KnowledgeBase::addUser(const char* question, const char* answer, const char* category) {
    if (!question || !question[0] || !answer || !answer[0]) return -1;
    if (strlen(question) >= AMELTECH_MAX_QUESTION_LEN) return -5;
    if (strlen(answer) >= AMELTECH_MAX_ANSWER_LEN) return -5;
    if (category && strlen(category) >= AMELTECH_MAX_CATEGORY_LEN) return -5;

    char norm[AMELTECH_MAX_QUESTION_LEN];
    normalize(question, norm, sizeof(norm));
    if (!norm[0]) return -1;

    // Check duplicates / conflicts in user store
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i].used) continue;
        if (strcmp(_user[i].normalized, norm) == 0) {
            if (answersConflict(_user[i].answer, answer)) return -3;  // conflict
            return -2;  // duplicate
        }
        // near-duplicate
        float sc = scoreCandidate(norm, _user[i].normalized, _user[i].question);
        if (sc >= 0.92f) {
            if (answersConflict(_user[i].answer, answer)) return -3;
            return -2;
        }
    }

    // Find free slot
    int slot = -1;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i].used) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0) return -4;  // full

    strncpy(_user[slot].question, question, AMELTECH_MAX_QUESTION_LEN - 1);
    _user[slot].question[AMELTECH_MAX_QUESTION_LEN - 1] = '\0';
    strncpy(_user[slot].answer, answer, AMELTECH_MAX_ANSWER_LEN - 1);
    _user[slot].answer[AMELTECH_MAX_ANSWER_LEN - 1] = '\0';
    if (category && category[0]) {
        strncpy(_user[slot].category, category, AMELTECH_MAX_CATEGORY_LEN - 1);
    } else {
        strncpy(_user[slot].category, "custom", AMELTECH_MAX_CATEGORY_LEN - 1);
    }
    _user[slot].category[AMELTECH_MAX_CATEGORY_LEN - 1] = '\0';
    strncpy(_user[slot].normalized, norm, AMELTECH_MAX_QUESTION_LEN - 1);
    _user[slot].normalized[AMELTECH_MAX_QUESTION_LEN - 1] = '\0';
    _user[slot].used = true;
    return 0;
}

int8_t KnowledgeBase::removeUser(const char* question) {
    if (!question) return -1;
    char norm[AMELTECH_MAX_QUESTION_LEN];
    normalize(question, norm, sizeof(norm));
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i].used) continue;
        if (strcmp(_user[i].normalized, norm) == 0) {
            _user[i].used = false;
            return 0;
        }
    }
    return -1;
}

int8_t KnowledgeBase::saveToNvs() {
#if defined(ESP32)
    Preferences prefs;
    if (!prefs.begin("ameltech_kb", false)) return -1;
    uint8_t count = 0;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (_user[i].used) ++count;
    }
    prefs.putUChar("cnt", count);
    uint8_t idx = 0;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES && idx < count; ++i) {
        if (!_user[i].used) continue;
        char keyq[8], keya[8], keyc[8];
        snprintf(keyq, sizeof(keyq), "q%u", idx);
        snprintf(keya, sizeof(keya), "a%u", idx);
        snprintf(keyc, sizeof(keyc), "c%u", idx);
        prefs.putString(keyq, _user[i].question);
        prefs.putString(keya, _user[i].answer);
        prefs.putString(keyc, _user[i].category);
        ++idx;
    }
    prefs.end();
    return 0;
#else
    return -1;  // NVS not available on host
#endif
}

int8_t KnowledgeBase::loadFromNvs() {
#if defined(ESP32)
    Preferences prefs;
    if (!prefs.begin("ameltech_kb", true)) return -1;
    uint8_t count = prefs.getUChar("cnt", 0);
    if (count > AMELTECH_MAX_USER_ENTRIES) count = AMELTECH_MAX_USER_ENTRIES;
    clearUser();
    for (uint8_t idx = 0; idx < count; ++idx) {
        char keyq[8], keya[8], keyc[8];
        snprintf(keyq, sizeof(keyq), "q%u", idx);
        snprintf(keya, sizeof(keya), "a%u", idx);
        snprintf(keyc, sizeof(keyc), "c%u", idx);
        String q = prefs.getString(keyq, "");
        String a = prefs.getString(keya, "");
        String c = prefs.getString(keyc, "custom");
        if (q.length() > 0 && a.length() > 0) {
            addUser(q.c_str(), a.c_str(), c.c_str());
        }
    }
    prefs.end();
    return 0;
#else
    return -1;
#endif
}
