// =============================================================
// KnowledgeBase.cpp
// =============================================================
#include "KnowledgeBase.h"
#include "knowledge_generated.h"

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
#include <Preferences.h>
#define AMELTECH_HAVE_NVS 1
#else
#define AMELTECH_HAVE_NVS 0
#endif

// ---------------------------------------------------------------
// Construction
// ---------------------------------------------------------------
KnowledgeBase::KnowledgeBase() : _userCount(0) {
    for (uint8_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; i++) {
        _userEntries[i].question[0] = '\0';
        _userEntries[i].answer[0] = '\0';
        _userEntries[i].category[0] = '\0';
        _userEntries[i].isUserEntry = true;
    }
}

void KnowledgeBase::begin() {
    // Built-in knowledge lives in flash (knowledge_generated.h) and is
    // read directly during query(); nothing to copy into RAM here.
    // User knowledge is populated later via loadFromNVS()/train().
}

// ---------------------------------------------------------------
// Normalization (spec item 6)
//   - lowercasing
//   - whitespace normalization
//   - punctuation stripping
//   - common abbreviation expansion
// ---------------------------------------------------------------
static String expandAbbreviations(const String& s) {
    // Token-boundary-safe common abbreviation expansions relevant to
    // the built-in dataset and general chat use. Kept small and
    // deterministic (no guessing).
    struct Abbrev { const char* from; const char* to; };
    static const Abbrev table[] = {
        {" r ", " are "},
        {" u ", " you "},
        {" ur ", " your "},
        {" sec ", " seconds "},
        {" secs ", " seconds "},
        {" min ", " minutes "},
        {" mins ", " minutes "},
        {" hr ", " hours "},
        {" hrs ", " hours "},
        {" wifi ", " wifi "},
        {" esp ", " esp32 "},
        {" whats ", " what is "},
        {" what's ", " what is "},
        {" how many secs ", " how many seconds "},
    };
    String padded = " " + s + " ";
    for (auto& a : table) {
        padded.replace(a.from, a.to);
    }
    padded.trim();
    return padded;
}

String KnowledgeBase::normalize(const String& input) {
    String s = input;
    s.trim();
    s.toLowerCase();

    // Strip punctuation (keep alphanumerics, spaces, and '%','+','-','*','/','(',')','.' for
    // calculator-adjacent text; KnowledgeBase normalization strips those too since this
    // path is for Q&A matching, not calculation).
    String cleaned;
    cleaned.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c) || c == ' ') {
            cleaned += c;
        } else if (c == '\'' ) {
            // drop apostrophes so "what's" -> "whats" (handled by abbreviation table too)
            continue;
        } else {
            cleaned += ' ';
        }
    }

    // Collapse whitespace
    String collapsed;
    collapsed.reserve(cleaned.length());
    bool lastWasSpace = false;
    for (size_t i = 0; i < cleaned.length(); i++) {
        char c = cleaned[i];
        if (c == ' ') {
            if (!lastWasSpace) collapsed += ' ';
            lastWasSpace = true;
        } else {
            collapsed += c;
            lastWasSpace = false;
        }
    }
    collapsed.trim();

    String expanded = expandAbbreviations(collapsed);

    // Deterministic numeral normalization: convert digit tokens 1-10 to
    // their word form, since the built-in dataset is authored with
    // written-out numbers ("one minute", "sixty seconds" is NOT used,
    // but "one minute"/"one hour"/"one day"/"one week" are). This is a
    // small, fixed lookup table — not a general numeral parser — so it
    // remains fully deterministic and bounded.
    struct NumWord { const char* digit; const char* word; };
    static const NumWord numWords[] = {
        {"1", "one"}, {"2", "two"}, {"3", "three"}, {"4", "four"}, {"5", "five"},
        {"6", "six"}, {"7", "seven"}, {"8", "eight"}, {"9", "nine"}, {"10", "ten"}
    };

    String finalTokens;
    finalTokens.reserve(expanded.length());
    {
        String tokens[AMELTECH_MAX_TOKENS];
        uint8_t rawCount = 0;
        // Local raw split (not the stopword-filtering tokenize()) so we
        // preserve every token including single-letter/stopword tokens
        // for faithful reconstruction of the normalized string.
        int start = 0;
        int len = expanded.length();
        while (start < len && rawCount < AMELTECH_MAX_TOKENS) {
            int spaceIdx = expanded.indexOf(' ', start);
            String tok;
            if (spaceIdx == -1) {
                tok = expanded.substring(start);
                start = len;
            } else {
                tok = expanded.substring(start, spaceIdx);
                start = spaceIdx + 1;
            }
            if (tok.length() > 0) {
                // Numeral -> word substitution
                for (auto& nw : numWords) {
                    if (tok == String(nw.digit)) {
                        tok = String(nw.word);
                        break;
                    }
                }
                // Simple plural trimming for common unit words so
                // "minutes"/"minute", "seconds"/"second", "hours"/"hour",
                // "days"/"day" match uniformly. Deliberately narrow in
                // scope (whitelist) to avoid corrupting unrelated words.
                static const char* pluralUnits[] = {
                    "seconds", "minutes", "hours", "days", "weeks", "months", "years"
                };
                for (auto& pu : pluralUnits) {
                    if (tok == String(pu)) {
                        String singular = tok.substring(0, tok.length() - 1);
                        tok = singular;
                        break;
                    }
                }
                tokens[rawCount++] = tok;
            }
        }
        for (uint8_t i = 0; i < rawCount; i++) {
            if (i > 0) finalTokens += " ";
            finalTokens += tokens[i];
        }
    }

    return finalTokens;
}

// ---------------------------------------------------------------
// Tokenization
// ---------------------------------------------------------------
void KnowledgeBase::tokenize(const String& normalized, String outTokens[AMELTECH_MAX_TOKENS], uint8_t& outCount) {
    outCount = 0;
    int start = 0;
    int len = normalized.length();
    while (start < len && outCount < AMELTECH_MAX_TOKENS) {
        int spaceIdx = normalized.indexOf(' ', start);
        String tok;
        if (spaceIdx == -1) {
            tok = normalized.substring(start);
            start = len;
        } else {
            tok = normalized.substring(start, spaceIdx);
            start = spaceIdx + 1;
        }
        if (tok.length() > 0) {
            // Skip very common stopwords that add little discriminative value
            if (tok != "is" && tok != "a" && tok != "the" && tok != "of" && tok != "in") {
                outTokens[outCount++] = tok;
            } else if (outCount == 0) {
                // keep at least something if the whole input is stopwords
                outTokens[outCount++] = tok;
            }
        }
    }
}

// ---------------------------------------------------------------
// Levenshtein distance, bounded (spec: safe fuzzy-search limits)
// ---------------------------------------------------------------
uint16_t KnowledgeBase::levenshtein(const String& a, const String& b, uint16_t maxDistance) {
    uint16_t la = a.length();
    uint16_t lb = b.length();

    if (la == 0) return lb;
    if (lb == 0) return la;
    // Bound the work: if length difference alone exceeds maxDistance, bail early.
    uint16_t lenDiff = (la > lb) ? (la - lb) : (lb - la);
    if (lenDiff > maxDistance) return (uint16_t)(maxDistance + 1);

    // Bounded-width DP (only two rows kept), capped at a hard ceiling
    // to guarantee bounded memory/time regardless of input length.
    const uint16_t HARD_CAP = 128;
    uint16_t caLen = la > HARD_CAP ? HARD_CAP : la;
    uint16_t cbLen = lb > HARD_CAP ? HARD_CAP : lb;

    static uint16_t prevRow[129];
    static uint16_t curRow[129];

    for (uint16_t j = 0; j <= cbLen; j++) prevRow[j] = j;

    for (uint16_t i = 1; i <= caLen; i++) {
        curRow[0] = i;
        char ca = a[i - 1];
        uint16_t rowMin = curRow[0];
        for (uint16_t j = 1; j <= cbLen; j++) {
            char cb = b[j - 1];
            uint16_t cost = (ca == cb) ? 0 : 1;
            uint16_t del = prevRow[j] + 1;
            uint16_t ins = curRow[j - 1] + 1;
            uint16_t sub = prevRow[j - 1] + cost;
            uint16_t best = del < ins ? del : ins;
            best = best < sub ? best : sub;
            curRow[j] = best;
            if (best < rowMin) rowMin = best;
        }
        if (rowMin > maxDistance) {
            return (uint16_t)(maxDistance + 1); // early exit: hopeless branch
        }
        for (uint16_t j = 0; j <= cbLen; j++) prevRow[j] = curRow[j];
    }

    return prevRow[cbLen];
}

// ---------------------------------------------------------------
// Lightweight semantic-style similarity: blends token (Jaccard-like)
// overlap with normalized edit-distance. This is explicitly NOT a
// trained embedding model — see README limitations.
// ---------------------------------------------------------------
float KnowledgeBase::similarity(const String& a, const String& b) {
    if (a == b) return 1.0f;
    if (a.length() == 0 || b.length() == 0) return 0.0f;

    String tokensA[AMELTECH_MAX_TOKENS];
    String tokensB[AMELTECH_MAX_TOKENS];
    uint8_t countA = 0, countB = 0;
    tokenize(a, tokensA, countA);
    tokenize(b, tokensB, countB);

    uint8_t overlap = 0;
    for (uint8_t i = 0; i < countA; i++) {
        for (uint8_t j = 0; j < countB; j++) {
            if (tokensA[i] == tokensB[j]) { overlap++; break; }
        }
    }
    uint8_t unionCount = countA + countB - overlap;
    float jaccard = unionCount > 0 ? ((float)overlap / (float)unionCount) : 0.0f;

    uint16_t maxLen = a.length() > b.length() ? a.length() : b.length();
    uint16_t maxDist = maxLen; // allow full-range computation, bounded internally
    uint16_t dist = levenshtein(a, b, maxDist > 40 ? 40 : maxDist);
    float editSim = maxLen > 0 ? (1.0f - ((float)dist / (float)maxLen)) : 0.0f;
    if (editSim < 0) editSim = 0;

    // Blend: token overlap is a stronger signal for short Q&A phrases.
    float blended = (0.65f * jaccard) + (0.35f * editSim);
    if (blended > 1.0f) blended = 1.0f;
    if (blended < 0.0f) blended = 0.0f;
    return blended;
}

// ---------------------------------------------------------------
// Scoring a single candidate question against normalized input
// ---------------------------------------------------------------
bool KnowledgeBase::_scoreAgainst(const String& normalizedInput, const String inputTokens[AMELTECH_MAX_TOKENS],
                                   uint8_t inputTokenCount, const char* candidateQuestion, float& outScore) const {
    String candidate(candidateQuestion);
    String normalizedCandidate = normalize(candidate);

    // Exact match -> maximum confidence
    if (normalizedInput == normalizedCandidate) {
        outScore = 1.0f;
        return true;
    }

    // Keyword match bonus: fraction of candidate tokens present in input
    String candTokens[AMELTECH_MAX_TOKENS];
    uint8_t candCount = 0;
    tokenize(normalizedCandidate, candTokens, candCount);

    uint8_t keywordHits = 0;
    for (uint8_t i = 0; i < candCount; i++) {
        for (uint8_t j = 0; j < inputTokenCount; j++) {
            if (candTokens[i] == inputTokens[j]) { keywordHits++; break; }
        }
    }
    float keywordScore = candCount > 0 ? ((float)keywordHits / (float)candCount) : 0.0f;

    // Lightweight semantic-style similarity (token overlap + edit distance)
    float simScore = similarity(normalizedInput, normalizedCandidate);

    // Weighted blend biased toward keyword coverage, since these are
    // short-form Q&A phrases rather than free-form sentences.
    float score = (0.5f * keywordScore) + (0.5f * simScore);
    if (score > 0.995f) score = 0.995f; // reserve 1.0 strictly for exact matches

    outScore = score;
    return score > 0.0f;
}

// ---------------------------------------------------------------
// Main query pipeline
// ---------------------------------------------------------------
MatchResult KnowledgeBase::query(const String& rawInput) const {
    MatchResult result;
    result.found = false;
    result.confidence = 0.0f;
    result.fromUserKnowledge = false;

    String normalizedInput = normalize(rawInput);
    if (normalizedInput.length() == 0) {
        return result;
    }

    String inputTokens[AMELTECH_MAX_TOKENS];
    uint8_t inputTokenCount = 0;
    tokenize(normalizedInput, inputTokens, inputTokenCount);

    float bestScore = 0.0f;
    String bestAnswer;
    String bestCategory;
    String bestQuestion;
    bool bestIsUser = false;

    // --- Search built-in (flash-resident) knowledge first ---
    for (uint16_t i = 0; i < AMELTECH_GENERATED_KNOWLEDGE_COUNT; i++) {
        char qBuf[AMELTECH_MAX_QUESTION_LEN + 1];
        strncpy_P(qBuf, (PGM_P)pgm_read_ptr(&AMELTECH_GENERATED_KNOWLEDGE[i].question), AMELTECH_MAX_QUESTION_LEN);
        qBuf[AMELTECH_MAX_QUESTION_LEN] = '\0';

        float score = 0.0f;
        if (_scoreAgainst(normalizedInput, inputTokens, inputTokenCount, qBuf, score)) {
            if (score > bestScore) {
                bestScore = score;

                char aBuf[AMELTECH_MAX_ANSWER_LEN + 1];
                strncpy_P(aBuf, (PGM_P)pgm_read_ptr(&AMELTECH_GENERATED_KNOWLEDGE[i].answer), AMELTECH_MAX_ANSWER_LEN);
                aBuf[AMELTECH_MAX_ANSWER_LEN] = '\0';

                char cBuf[AMELTECH_MAX_CATEGORY_LEN + 1];
                strncpy_P(cBuf, (PGM_P)pgm_read_ptr(&AMELTECH_GENERATED_KNOWLEDGE[i].category), AMELTECH_MAX_CATEGORY_LEN);
                cBuf[AMELTECH_MAX_CATEGORY_LEN] = '\0';

                bestAnswer = String(aBuf);
                bestCategory = String(cBuf);
                bestQuestion = String(qBuf);
                bestIsUser = false;
            }
        }
    }

    // --- Search user-trained knowledge (RAM) — user knowledge can
    //     override/extend built-in answers when it scores higher ---
    for (uint8_t i = 0; i < _userCount; i++) {
        float score = 0.0f;
        if (_scoreAgainst(normalizedInput, inputTokens, inputTokenCount, _userEntries[i].question, score)) {
            if (score > bestScore) {
                bestScore = score;
                bestAnswer = String(_userEntries[i].answer);
                bestCategory = String(_userEntries[i].category);
                bestQuestion = String(_userEntries[i].question);
                bestIsUser = true;
            }
        }
    }

    result.found = bestScore > 0.0f;
    result.confidence = bestScore;
    result.answer = bestAnswer;
    result.category = bestCategory;
    result.matchedQuestion = bestQuestion;
    result.fromUserKnowledge = bestIsUser;
    return result;
}

// ---------------------------------------------------------------
// Category validation
// ---------------------------------------------------------------
bool KnowledgeBase::_isValidCategory(const String& category) const {
    if (category.length() == 0 || category.length() > AMELTECH_MAX_CATEGORY_LEN) return false;
    // Open category set for user training, but reject obviously malformed
    // categories (whitespace-only, control characters).
    for (size_t i = 0; i < category.length(); i++) {
        char c = category[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    }
    return true;
}

int KnowledgeBase::_findUserEntryIndex(const String& normalizedQuestion) const {
    for (uint8_t i = 0; i < _userCount; i++) {
        if (normalize(String(_userEntries[i].question)) == normalizedQuestion) {
            return i;
        }
    }
    return -1;
}

bool KnowledgeBase::_isNormalizedDuplicateOfBuiltIn(const String& normalizedQuestion, String& outExistingAnswer) const {
    for (uint16_t i = 0; i < AMELTECH_GENERATED_KNOWLEDGE_COUNT; i++) {
        char qBuf[AMELTECH_MAX_QUESTION_LEN + 1];
        strncpy_P(qBuf, (PGM_P)pgm_read_ptr(&AMELTECH_GENERATED_KNOWLEDGE[i].question), AMELTECH_MAX_QUESTION_LEN);
        qBuf[AMELTECH_MAX_QUESTION_LEN] = '\0';
        if (normalize(String(qBuf)) == normalizedQuestion) {
            char aBuf[AMELTECH_MAX_ANSWER_LEN + 1];
            strncpy_P(aBuf, (PGM_P)pgm_read_ptr(&AMELTECH_GENERATED_KNOWLEDGE[i].answer), AMELTECH_MAX_ANSWER_LEN);
            aBuf[AMELTECH_MAX_ANSWER_LEN] = '\0';
            outExistingAnswer = String(aBuf);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------
// Training (spec items 8 & 9): validation + contradiction checking
// ---------------------------------------------------------------
TrainResult KnowledgeBase::train(const String& question, const String& answer, const String& category) {
    TrainResult result;

    String trimmedQ = question;
    String trimmedA = answer;
    trimmedQ.trim();
    trimmedA.trim();

    if (trimmedQ.length() == 0) {
        result.status = AMELTECH_INVALID_INPUT;
        result.message = "Question is empty.";
        return result;
    }
    if (trimmedA.length() == 0) {
        result.status = AMELTECH_INVALID_INPUT;
        result.message = "Answer is empty.";
        return result;
    }
    if (trimmedQ.length() > AMELTECH_MAX_QUESTION_LEN) {
        result.status = AMELTECH_INVALID_INPUT;
        result.message = "Question exceeds maximum length of " + String(AMELTECH_MAX_QUESTION_LEN) + " characters.";
        return result;
    }
    if (trimmedA.length() > AMELTECH_MAX_ANSWER_LEN) {
        result.status = AMELTECH_INVALID_INPUT;
        result.message = "Answer exceeds maximum length of " + String(AMELTECH_MAX_ANSWER_LEN) + " characters.";
        return result;
    }
    if (!_isValidCategory(category)) {
        result.status = AMELTECH_INVALID_CONFIGURATION;
        result.message = "Invalid category: must be alphanumeric (with '_'/'-'), <= " +
                          String(AMELTECH_MAX_CATEGORY_LEN) + " characters.";
        return result;
    }

    String normalizedQ = normalize(trimmedQ);

    // Duplicate / contradiction check against user knowledge
    int existingIdx = _findUserEntryIndex(normalizedQ);
    if (existingIdx >= 0) {
        String existingAnswer = String(_userEntries[existingIdx].answer);
        if (existingAnswer == trimmedA) {
            result.status = AMELTECH_DUPLICATE;
            result.message = "This question/answer pair is already known (exact duplicate).";
            return result;
        } else {
            result.status = AMELTECH_CONTRADICTION;
            result.message = "A different answer is already stored for this question. "
                              "Use removeQA() first if you intend to replace it.";
            return result;
        }
    }

    // Duplicate / contradiction check against built-in knowledge
    String builtInAnswer;
    if (_isNormalizedDuplicateOfBuiltIn(normalizedQ, builtInAnswer)) {
        if (builtInAnswer == trimmedA) {
            result.status = AMELTECH_DUPLICATE;
            result.message = "This question is already answered identically by built-in knowledge.";
            return result;
        } else {
            result.status = AMELTECH_CONTRADICTION;
            result.message = "This question conflicts with an existing built-in answer. "
                              "Built-in knowledge is not overwritten; choose a more specific question.";
            return result;
        }
    }

    // Near-duplicate (fuzzy) contradiction check against user knowledge:
    // catches paraphrases like "what is my project" vs "what's my project"
    for (uint8_t i = 0; i < _userCount; i++) {
        float sim = similarity(normalizedQ, normalize(String(_userEntries[i].question)));
        if (sim >= 0.90f && sim < 1.0f) {
            String existingAnswer = String(_userEntries[i].answer);
            if (existingAnswer != trimmedA) {
                result.status = AMELTECH_CONTRADICTION;
                result.message = "A near-duplicate question ('" + String(_userEntries[i].question) +
                                  "') already has a different answer stored.";
                return result;
            }
        }
    }

    if (_userCount >= AMELTECH_MAX_USER_ENTRIES) {
        result.status = AMELTECH_MEMORY_ERROR;
        result.message = "User knowledge storage is full (" + String(AMELTECH_MAX_USER_ENTRIES) + " entries max).";
        return result;
    }

    // Store
    KnowledgeEntry& entry = _userEntries[_userCount];
    trimmedQ.toCharArray(entry.question, AMELTECH_MAX_QUESTION_LEN + 1);
    trimmedA.toCharArray(entry.answer, AMELTECH_MAX_ANSWER_LEN + 1);
    category.toCharArray(entry.category, AMELTECH_MAX_CATEGORY_LEN + 1);
    entry.isUserEntry = true;
    _userCount++;

    result.status = AMELTECH_OK;
    result.message = "Trained successfully.";
    return result;
}

TrainResult KnowledgeBase::addQA(const String& question, const String& answer, const String& category) {
    return train(question, answer, category);
}

AmelTechStatus KnowledgeBase::removeQA(const String& question) {
    String normalizedQ = normalize(question);
    int idx = _findUserEntryIndex(normalizedQ);
    if (idx < 0) {
        return AMELTECH_NOT_FOUND;
    }
    // Shift remaining entries down (bounded, small array)
    for (uint8_t i = idx; i < _userCount - 1; i++) {
        _userEntries[i] = _userEntries[i + 1];
    }
    _userCount--;
    _userEntries[_userCount].question[0] = '\0';
    _userEntries[_userCount].answer[0] = '\0';
    _userEntries[_userCount].category[0] = '\0';
    return AMELTECH_OK;
}

void KnowledgeBase::clearUserKnowledge() {
    _userCount = 0;
    for (uint8_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; i++) {
        _userEntries[i].question[0] = '\0';
        _userEntries[i].answer[0] = '\0';
        _userEntries[i].category[0] = '\0';
    }
}

size_t KnowledgeBase::builtInCount() const {
    return AMELTECH_GENERATED_KNOWLEDGE_COUNT;
}

size_t KnowledgeBase::userCount() const {
    return _userCount;
}

size_t KnowledgeBase::totalCount() const {
    return builtInCount() + userCount();
}

// ---------------------------------------------------------------
// Persistence via ESP32 Preferences (NVS). On non-ESP32 host builds
// (tests), this is a documented no-op returning AMELTECH_UNSUPPORTED.
// ---------------------------------------------------------------
AmelTechStatus KnowledgeBase::saveToNVS() const {
#if AMELTECH_HAVE_NVS
    Preferences prefs;
    if (!prefs.begin("ameltech_kb", false)) {
        return AMELTECH_STORAGE_ERROR;
    }
    prefs.putUChar("count", _userCount);
    for (uint8_t i = 0; i < _userCount; i++) {
        String qKey = "q" + String(i);
        String aKey = "a" + String(i);
        String cKey = "c" + String(i);
        prefs.putString(qKey.c_str(), _userEntries[i].question);
        prefs.putString(aKey.c_str(), _userEntries[i].answer);
        prefs.putString(cKey.c_str(), _userEntries[i].category);
    }
    prefs.end();
    return AMELTECH_OK;
#else
    return AMELTECH_UNSUPPORTED;
#endif
}

AmelTechStatus KnowledgeBase::loadFromNVS() {
#if AMELTECH_HAVE_NVS
    Preferences prefs;
    if (!prefs.begin("ameltech_kb", true)) {
        return AMELTECH_STORAGE_ERROR;
    }
    uint8_t count = prefs.getUChar("count", 0);
    if (count > AMELTECH_MAX_USER_ENTRIES) count = AMELTECH_MAX_USER_ENTRIES;

    _userCount = 0;
    for (uint8_t i = 0; i < count; i++) {
        String qKey = "q" + String(i);
        String aKey = "a" + String(i);
        String cKey = "c" + String(i);
        String q = prefs.getString(qKey.c_str(), "");
        String a = prefs.getString(aKey.c_str(), "");
        String c = prefs.getString(cKey.c_str(), "custom");
        if (q.length() == 0 || a.length() == 0) continue;

        KnowledgeEntry& entry = _userEntries[_userCount];
        q.toCharArray(entry.question, AMELTECH_MAX_QUESTION_LEN + 1);
        a.toCharArray(entry.answer, AMELTECH_MAX_ANSWER_LEN + 1);
        c.toCharArray(entry.category, AMELTECH_MAX_CATEGORY_LEN + 1);
        entry.isUserEntry = true;
        _userCount++;
    }
    prefs.end();
    return AMELTECH_OK;
#else
    return AMELTECH_UNSUPPORTED;
#endif
}
