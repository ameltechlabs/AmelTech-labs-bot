#include "KnowledgeBase.h"
#include "AmelTechLog.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if AMELTECH_NVS_AVAILABLE
#include <Preferences.h>
#endif

// ---------------------------------------------------------------------------
KnowledgeBase::KnowledgeBase()
    : _userCount(0),
      _nextCode(AMELTECH_TRAIN_FIRST_CODE),
      _dirty(false),
      _minFreeHeap(AMELTECH_TRAIN_MIN_FREE_HEAP),
      _cacheValid(false),
      _lastScanMicros(0),
      _lastCandidates(0),
      _lastFallback(false),
      _cacheHits(0),
      _queries(0) {
    memset(_user, 0, sizeof(_user));
    memset(&_cacheValue, 0, sizeof(_cacheValue));
    _cacheKey[0] = '\0';
}

KnowledgeBase::~KnowledgeBase() {
    clearUser();
}

uint32_t KnowledgeBase::freeHeap() {
#if defined(ESP32)
    return ESP.getFreeHeap();
#else
    return 0xFFFFFFFFUL;
#endif
}

const char* KnowledgeBase::addResultToString(int8_t rc) {
    switch (rc) {
        case KB_ADD_OK:        return "stored";
        case KB_ADD_INVALID:   return "question or answer is empty";
        case KB_ADD_DUPLICATE: return "this question is already trained";
        case KB_ADD_CONFLICT:  return "a similar question already has a different answer";
        case KB_ADD_FULL:      return "user knowledge is full";
        case KB_ADD_TOO_LARGE: return "question or answer exceeds the size limit";
        case KB_ADD_NO_MEMORY: return "not enough heap to allocate the entry";
        case KB_ADD_HEAP_GUARD:return "free heap is below the reserved minimum";
        default:               return "unknown error";
    }
}

bool KnowledgeBase::begin() {
    clearUser();
    _nextCode = AMELTECH_TRAIN_FIRST_CODE;
    loadFromNvs();
    _dirty = false;
    return true;
}

void KnowledgeBase::end() {
    clearUser();
}

void KnowledgeBase::invalidateCache() {
    _cacheValid = false;
    _cacheKey[0] = '\0';
    memset(&_cacheValue, 0, sizeof(_cacheValue));
}

void KnowledgeBase::clearUser() {
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (_user[i]) {
            free(_user[i]);
            _user[i] = nullptr;
        }
    }
    _userCount = 0;
    invalidateCache();
    _dirty = true;
}

const UserKnowledgeEntry* KnowledgeBase::userAt(size_t index) const {
    size_t seen = 0;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i]) continue;
        if (seen == index) return _user[i];
        ++seen;
    }
    return nullptr;
}

int KnowledgeBase::findUserSlotByCode(uint16_t code) const {
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (_user[i] && _user[i]->code == code) return (int)i;
    }
    return -1;
}

const UserKnowledgeEntry* KnowledgeBase::userByCode(uint16_t code) const {
    int slot = findUserSlotByCode(code);
    return slot >= 0 ? _user[slot] : nullptr;
}

uint16_t KnowledgeBase::allocateCode() {
    // Find the next free code, wrapping at 9999 so codes stay 4 digits.
    for (uint16_t attempts = 0; attempts <= AMELTECH_TRAIN_MAX_CODE; ++attempts) {
        uint16_t candidate = _nextCode;
        _nextCode = (uint16_t)((_nextCode >= AMELTECH_TRAIN_MAX_CODE)
                                   ? AMELTECH_TRAIN_FIRST_CODE
                                   : _nextCode + 1);
        if (candidate == 0) continue;
        if (findUserSlotByCode(candidate) < 0) return candidate;
    }
    return 0;  // every code taken (impossible with 48 slots)
}

// ---------------------------------------------------------------------------
// Candidate pool: small insertion-sorted array, descending by score.
// ---------------------------------------------------------------------------
void KnowledgeBase::insertCandidate(Candidate* pool, uint8_t& used, uint8_t cap,
                                    float pre, int32_t index, bool user) {
    if (used == cap && pre <= pool[used - 1].pre) return;

    uint8_t pos = used;
    if (used == cap) pos = (uint8_t)(cap - 1);
    else ++used;

    while (pos > 0 && pool[pos - 1].pre < pre) {
        pool[pos] = pool[pos - 1];
        --pos;
    }
    pool[pos].pre = pre;
    pool[pos].index = index;
    pool[pos].user = user;
}

void KnowledgeBase::fillResultFromBuiltin(MatchResult& r, int32_t idx) const {
    const AmelTechBuiltinEntry& e = AMELTECH_BUILTIN_KNOWLEDGE[idx];
    r.answer = e.answer;
    r.category = e.category;
    r.matchedQuestion = e.question;
    r.matchedNormalized = e.normalized;
    r.fromUser = false;
    r.code = 0;
    r.index = idx;
}

void KnowledgeBase::fillResultFromUser(MatchResult& r, int32_t idx) const {
    const UserKnowledgeEntry* e = _user[idx];
    if (!e) return;
    r.answer = e->answer;
    r.category = e->category;
    r.matchedQuestion = e->question;
    r.matchedNormalized = e->normalized;
    r.fromUser = true;
    r.code = e->code;
    r.index = idx;
}

// ---------------------------------------------------------------------------
uint8_t KnowledgeBase::rank(const AmelTechQuery& q, MatchResult* out, uint8_t maxOut) {
    if (!out || maxOut == 0) return 0;
    for (uint8_t i = 0; i < maxOut; ++i) {
        memset(&out[i], 0, sizeof(MatchResult));
        out[i].index = -1;
    }
    if (!q.valid) return 0;

    uint32_t t0 = micros();

    Candidate pool[AMELTECH_CANDIDATE_POOL];
    uint8_t poolUsed = 0;
    memset(pool, 0, sizeof(pool));

    // ---- Stage 1a: signature-gated sweep over user knowledge -------------
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        const UserKnowledgeEntry* e = _user[i];
        if (!e) continue;
        float pre = NeuralEngine::prefilterScore(q, e->signature, e->bloom, e->tokenCount);
        // User entries are few and deliberately taught: never gate them out.
        insertCandidate(pool, poolUsed, AMELTECH_CANDIDATE_POOL, pre + 0.02f, (int32_t)i, true);
    }

    // ---- Stage 1b: signature-gated sweep over built-in knowledge ---------
    uint32_t scanned = 0;
    for (size_t i = 0; i < AMELTECH_BUILTIN_KNOWLEDGE_COUNT; ++i) {
        const AmelTechBuiltinEntry& e = AMELTECH_BUILTIN_KNOWLEDGE[i];
        if (q.signature && e.signature && (q.signature & e.signature) == 0) {
            // No content word in common: cannot be a strong match. Skipping
            // here is what makes a full sweep cost microseconds.
            continue;
        }
        float pre = NeuralEngine::prefilterScore(q, e.signature, e.bloom, e.tokenCount);
        insertCandidate(pool, poolUsed, AMELTECH_CANDIDATE_POOL, pre, (int32_t)i, false);

        if ((++scanned % AMELTECH_SCAN_YIELD_INTERVAL) == 0) AMELTECH_YIELD();
    }

    // ---- Stage 1c: typo fallback ----------------------------------------
    // A misspelled word hashes to a different signature bit, so the gate above
    // can hide the right row. When nothing promising survived, sweep again on
    // the trigram sketch alone, which tolerates spelling errors.
    _lastFallback = false;
    if (poolUsed == 0 || pool[0].pre < 0.40f) {
        _lastFallback = true;
        scanned = 0;
        for (size_t i = 0; i < AMELTECH_BUILTIN_KNOWLEDGE_COUNT; ++i) {
            const AmelTechBuiltinEntry& e = AMELTECH_BUILTIN_KNOWLEDGE[i];
            float dice = NeuralEngine::trigramDice(q.bloom, e.bloom);
            if (dice < 0.35f) continue;
            insertCandidate(pool, poolUsed, AMELTECH_CANDIDATE_POOL, dice * 0.95f, (int32_t)i, false);
            if ((++scanned % AMELTECH_SCAN_YIELD_INTERVAL) == 0) AMELTECH_YIELD();
        }
    }

    _lastCandidates = poolUsed;

    // ---- Stage 2: full scoring on the survivors --------------------------
    struct Scored {
        float conf;
        float raw;
        int32_t index;
        bool user;
    };
    Scored scored[AMELTECH_CANDIDATE_POOL];
    uint8_t scoredCount = 0;

    for (uint8_t i = 0; i < poolUsed; ++i) {
        const char* norm;
        uint32_t sig;
        uint64_t bloom;
        if (pool[i].user) {
            const UserKnowledgeEntry* e = _user[pool[i].index];
            if (!e) continue;
            norm = e->normalized;
            sig = e->signature;
            bloom = e->bloom;
        } else {
            const AmelTechBuiltinEntry& e = AMELTECH_BUILTIN_KNOWLEDGE[pool[i].index];
            norm = e.normalized;
            sig = e.signature;
            bloom = e.bloom;
        }
        float raw = NeuralEngine::fullScore(q, norm, sig, bloom);
        bool exact = (raw >= 0.9999f);
        float conf = NeuralEngine::calibrate(raw, exact);

        // A deliberately trained answer outranks a built-in one of equal merit.
        if (pool[i].user && conf < 1.0f) conf += 0.02f;
        if (conf > 1.0f) conf = 1.0f;

        // insertion sort into scored[]
        uint8_t pos = scoredCount;
        if (scoredCount < AMELTECH_CANDIDATE_POOL) ++scoredCount;
        else if (conf <= scored[scoredCount - 1].conf) continue;
        else pos = (uint8_t)(scoredCount - 1);

        while (pos > 0 && scored[pos - 1].conf < conf) {
            scored[pos] = scored[pos - 1];
            --pos;
        }
        scored[pos].conf = conf;
        scored[pos].raw = raw;
        scored[pos].index = pool[i].index;
        scored[pos].user = pool[i].user;
    }

    uint8_t n = scoredCount < maxOut ? scoredCount : maxOut;
    for (uint8_t i = 0; i < n; ++i) {
        MatchResult& r = out[i];
        r.confidence = scored[i].conf;
        r.rawScore = scored[i].raw;
        r.found = (scored[i].conf >= AMELTECH_CONF_WEAK);
        if (scored[i].user) fillResultFromUser(r, scored[i].index);
        else fillResultFromBuiltin(r, scored[i].index);
    }

    _lastScanMicros = micros() - t0;
    return n;
}

MatchResult KnowledgeBase::findBest(const AmelTechQuery& q, float minConfidence) {
    MatchResult none;
    memset(&none, 0, sizeof(none));
    none.index = -1;

    if (!q.valid) return none;
    ++_queries;

    if (_cacheValid && strcmp(_cacheKey, q.normalized) == 0) {
        ++_cacheHits;
        MatchResult r = _cacheValue;
        if (r.confidence < minConfidence) r.found = false;
        return r;
    }

    MatchResult best[1];
    uint8_t n = rank(q, best, 1);
    MatchResult r = (n > 0) ? best[0] : none;
    if (r.confidence < minConfidence) r.found = false;

    strncpy(_cacheKey, q.normalized, sizeof(_cacheKey) - 1);
    _cacheKey[sizeof(_cacheKey) - 1] = '\0';
    _cacheValue = r;
    _cacheValid = true;
    return r;
}

MatchResult KnowledgeBase::findBest(const char* question, float minConfidence) {
    AmelTechQuery q;
    NeuralEngine::buildQuery(question, q);
    return findBest(q, minConfidence);
}

// ---------------------------------------------------------------------------
bool KnowledgeBase::answersConflict(const char* a, const char* b) {
    if (!a || !b) return false;
    if (strcmp(a, b) == 0) return false;
    char na[AMELTECH_MAX_ANSWER_LEN];
    char nb[AMELTECH_MAX_ANSWER_LEN];
    AmelTechText::normalize(a, na, sizeof(na));
    AmelTechText::normalize(b, nb, sizeof(nb));
    if (strcmp(na, nb) == 0) return false;

    AmelTechQuery qa;
    NeuralEngine::buildQueryFromNormalized(na, qa);
    float sim = NeuralEngine::fullScore(qa, nb,
                                        AmelTechText::tokenSignature(nb),
                                        AmelTechText::trigramBloom(nb));
    return sim < 0.55f;
}

int8_t KnowledgeBase::addUser(const char* question, const char* answer,
                              const char* category, uint16_t* assignedCode) {
    if (assignedCode) *assignedCode = 0;

    if (!question || !answer) return KB_ADD_INVALID;

    char q[AMELTECH_MAX_QUESTION_LEN];
    char a[AMELTECH_MAX_ANSWER_LEN];
    AmelTechText::copyTrimmed(question, q, sizeof(q));
    AmelTechText::copyTrimmed(answer, a, sizeof(a));
    if (!q[0] || !a[0]) return KB_ADD_INVALID;

    if (strlen(question) >= AMELTECH_MAX_QUESTION_LEN) return KB_ADD_TOO_LARGE;
    if (strlen(answer) >= AMELTECH_MAX_ANSWER_LEN) return KB_ADD_TOO_LARGE;

    char cat[AMELTECH_MAX_CATEGORY_LEN];
    if (category && category[0]) {
        if (strlen(category) >= AMELTECH_MAX_CATEGORY_LEN) return KB_ADD_TOO_LARGE;
        AmelTechText::copyTrimmed(category, cat, sizeof(cat));
    } else {
        strncpy(cat, "custom", sizeof(cat) - 1);
        cat[sizeof(cat) - 1] = '\0';
    }
    if (!cat[0]) {
        strncpy(cat, "custom", sizeof(cat) - 1);
        cat[sizeof(cat) - 1] = '\0';
    }

    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(q, norm, sizeof(norm));
    if (!norm[0]) return KB_ADD_INVALID;

    // Reject duplicates and contradictions before touching the heap.
    AmelTechQuery nq;
    NeuralEngine::buildQueryFromNormalized(norm, nq);
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        const UserKnowledgeEntry* e = _user[i];
        if (!e) continue;
        if (strcmp(e->normalized, norm) == 0) {
            return answersConflict(e->answer, a) ? KB_ADD_CONFLICT : KB_ADD_DUPLICATE;
        }
        float sc = NeuralEngine::fullScore(nq, e->normalized, e->signature, e->bloom);
        if (sc >= 0.92f) {
            return answersConflict(e->answer, a) ? KB_ADD_CONFLICT : KB_ADD_DUPLICATE;
        }
    }

    int slot = -1;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i]) { slot = (int)i; break; }
    }
    if (slot < 0) return KB_ADD_FULL;

    // Heap guard: training must never eat into the memory the chat path and
    // its logging need.
    uint32_t heap = freeHeap();
    if (_minFreeHeap > 0 && heap != 0xFFFFFFFFUL) {
        if (heap < _minFreeHeap + sizeof(UserKnowledgeEntry)) return KB_ADD_HEAP_GUARD;
    }

    UserKnowledgeEntry* e = (UserKnowledgeEntry*)calloc(1, sizeof(UserKnowledgeEntry));
    if (!e) return KB_ADD_NO_MEMORY;

    strncpy(e->question, q, sizeof(e->question) - 1);
    strncpy(e->answer, a, sizeof(e->answer) - 1);
    strncpy(e->category, cat, sizeof(e->category) - 1);
    strncpy(e->normalized, norm, sizeof(e->normalized) - 1);
    e->signature = AmelTechText::tokenSignature(norm);
    e->bloom = AmelTechText::trigramBloom(norm);
    e->tokenCount = (uint8_t)nq.tokenCount;
    e->createdMs = millis();
    e->code = allocateCode();

    _user[slot] = e;
    ++_userCount;
    _dirty = true;
    invalidateCache();

    if (assignedCode) *assignedCode = e->code;
    AmelTechLogger.log(AMELTECH_LOG_INFO, "train code=%04u heap=%lu",
                       (unsigned)e->code, (unsigned long)freeHeap());
    return KB_ADD_OK;
}

int8_t KnowledgeBase::removeUser(const char* question) {
    if (!question) return -1;
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(question, norm, sizeof(norm));
    if (!norm[0]) return -1;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES; ++i) {
        if (!_user[i]) continue;
        if (strcmp(_user[i]->normalized, norm) == 0) {
            free(_user[i]);
            _user[i] = nullptr;
            --_userCount;
            _dirty = true;
            invalidateCache();
            return 0;
        }
    }
    return -1;
}

int8_t KnowledgeBase::removeUserByCode(uint16_t code) {
    int slot = findUserSlotByCode(code);
    if (slot < 0) return -1;
    free(_user[slot]);
    _user[slot] = nullptr;
    --_userCount;
    _dirty = true;
    invalidateCache();
    return 0;
}

// ---------------------------------------------------------------------------
// Persistence. Entries are written only when something actually changed, which
// matters because NVS sits in flash and flash has a finite erase budget.
// ---------------------------------------------------------------------------
int8_t KnowledgeBase::saveToNvs() {
#if AMELTECH_NVS_AVAILABLE
    Preferences prefs;
    if (!prefs.begin(AMELTECH_NVS_KB, false)) {
        AmelTechLogger.log(AMELTECH_LOG_ERROR, "NVS open failed (kb)");
        return -1;
    }

    uint8_t previous = prefs.getUChar("cnt", 0);
    uint8_t count = 0;
    for (size_t i = 0; i < AMELTECH_MAX_USER_ENTRIES && count < 255; ++i) {
        if (!_user[i]) continue;
        const UserKnowledgeEntry* e = _user[i];
        char keyq[16], keya[16], keyc[16], keyn[16];
        snprintf(keyq, sizeof(keyq), "q%u", (unsigned)count);
        snprintf(keya, sizeof(keya), "a%u", (unsigned)count);
        snprintf(keyc, sizeof(keyc), "c%u", (unsigned)count);
        snprintf(keyn, sizeof(keyn), "n%u", (unsigned)count);
        prefs.putString(keyq, e->question);
        prefs.putString(keya, e->answer);
        prefs.putString(keyc, e->category);
        prefs.putUShort(keyn, e->code);
        ++count;
    }

    // Remove keys left behind by a previously larger set.
    for (uint8_t i = count; i < previous; ++i) {
        char keyq[16], keya[16], keyc[16], keyn[16];
        unsigned idx = (unsigned)(i & 0xFF);
        snprintf(keyq, sizeof(keyq), "q%u", idx);
        snprintf(keya, sizeof(keya), "a%u", idx);
        snprintf(keyc, sizeof(keyc), "c%u", idx);
        snprintf(keyn, sizeof(keyn), "n%u", idx);
        prefs.remove(keyq);
        prefs.remove(keya);
        prefs.remove(keyc);
        prefs.remove(keyn);
    }

    prefs.putUChar("cnt", count);
    prefs.putUShort("next", _nextCode);
    prefs.end();
    _dirty = false;
    AmelTechLogger.log(AMELTECH_LOG_INFO, "kb saved n=%u", (unsigned)count);
    return 0;
#else
    _dirty = false;
    return -1;
#endif
}

int8_t KnowledgeBase::loadFromNvs() {
#if AMELTECH_NVS_AVAILABLE
    Preferences prefs;
    if (!prefs.begin(AMELTECH_NVS_KB, true)) {
        // Namespace does not exist yet: a first boot, not an error.
        return -1;
    }
    uint8_t count = prefs.getUChar("cnt", 0);
    uint16_t next = prefs.getUShort("next", AMELTECH_TRAIN_FIRST_CODE);
    if (count > AMELTECH_MAX_USER_ENTRIES) count = AMELTECH_MAX_USER_ENTRIES;

    clearUser();
    uint8_t restored = 0;
    for (uint8_t i = 0; i < count; ++i) {
        char keyq[16], keya[16], keyc[16], keyn[16];
        unsigned idx = (unsigned)(i & 0xFF);
        snprintf(keyq, sizeof(keyq), "q%u", idx);
        snprintf(keya, sizeof(keya), "a%u", idx);
        snprintf(keyc, sizeof(keyc), "c%u", idx);
        snprintf(keyn, sizeof(keyn), "n%u", idx);
        String q = prefs.getString(keyq, "");
        String a = prefs.getString(keya, "");
        String c = prefs.getString(keyc, "custom");
        uint16_t code = prefs.getUShort(keyn, 0);
        if (q.length() == 0 || a.length() == 0) continue;

        // Restoring must not be blocked by the training heap guard.
        uint32_t savedGuard = _minFreeHeap;
        _minFreeHeap = 0;
        uint16_t assigned = 0;
        int8_t rc = addUser(q.c_str(), a.c_str(), c.c_str(), &assigned);
        _minFreeHeap = savedGuard;

        if (rc == KB_ADD_OK && code != 0) {
            int slot = findUserSlotByCode(assigned);
            if (slot >= 0) _user[slot]->code = code;
            ++restored;
        }
    }
    prefs.end();

    if (next >= AMELTECH_TRAIN_FIRST_CODE && next <= AMELTECH_TRAIN_MAX_CODE) {
        _nextCode = next;
    }
    _dirty = false;
    invalidateCache();
    AmelTechLogger.log(AMELTECH_LOG_INFO, "kb loaded n=%u", (unsigned)restored);
    return 0;
#else
    return -1;
#endif
}
