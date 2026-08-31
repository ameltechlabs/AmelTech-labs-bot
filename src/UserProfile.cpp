#include "UserProfile.h"
#include "AmelTechLog.h"
#include "NeuralEngine.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#if AMELTECH_NVS_AVAILABLE
#include <Preferences.h>
#endif

// ---------------------------------------------------------------------------
// Words that follow "I am" but are states or feelings, never names.
// ---------------------------------------------------------------------------
static const char* const NOT_A_NAME[] = {
    "fine", "good", "great", "ok", "okay", "well", "bad", "sad", "happy",
    "tired", "sleepy", "hungry", "thirsty", "busy", "free", "ready", "sure",
    "sorry", "here", "there", "back", "home", "cold", "hot", "warm", "bored",
    "confused", "lost", "late", "early", "new", "old", "young", "alive",
    "not", "just", "still", "also", "very", "really", "so", "too", "quite",
    "from", "going", "doing", "working", "studying", "learning", "trying",
    "asking", "testing", "using", "making", "building", "looking", "waiting",
    "your", "you", "me", "my", "the", "a", "an", "and", "but", "with",
    "yes", "no", "hello", "hi", "hey", "thanks", "thank", "please",
    nullptr
};

// Occupations and studies: these belong in the field, not the name.
static const char* const FIELD_WORDS[] = {
    "student", "engineer", "engineering", "doctor", "teacher", "professor",
    "developer", "programmer", "coder", "designer", "scientist", "researcher",
    "technician", "electrician", "mechanic", "farmer", "driver", "nurse",
    "lawyer", "accountant", "manager", "architect", "artist", "musician",
    "writer", "chef", "pilot", "soldier", "officer", "analyst", "consultant",
    "intern", "apprentice", "hobbyist", "maker", "beginner", "learner",
    "developer", "tester", "founder", "freelancer", "graduate", "undergraduate",
    "schoolboy", "schoolgirl", "pupil", "scholar", "trainee", "worker",
    nullptr
};

static bool inList(const char* const* list, const char* word) {
    if (!word || !word[0]) return false;
    for (int i = 0; list[i]; ++i) {
        if (strcmp(list[i], word) == 0) return true;
    }
    return false;
}

static bool isAlphaWord(const char* w) {
    if (!w || !w[0]) return false;
    for (const char* p = w; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z'))) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
UserProfileStore::UserProfileStore() : _count(0), _seq(1), _dirty(false) {
    memset(_entries, 0, sizeof(_entries));
}

bool UserProfileStore::begin() {
    memset(_entries, 0, sizeof(_entries));
    _count = 0;
    _seq = 1;
    _dirty = false;
    load();
    return true;
}

void UserProfileStore::clear() {
    memset(_entries, 0, sizeof(_entries));
    _count = 0;
    _seq = 1;
    _dirty = true;
}

void UserProfileStore::titleCase(const char* in, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t j = 0;
    bool startOfWord = true;
    for (const char* p = in; *p && j < outSize - 1; ++p) {
        char c = *p;
        if (c == ' ' || c == '-' || c == '\'') {
            out[j++] = c;
            startOfWord = true;
            continue;
        }
        if (startOfWord) {
            out[j++] = (char)toupper((unsigned char)c);
            startOfWord = false;
        } else {
            out[j++] = (char)tolower((unsigned char)c);
        }
    }
    out[j] = '\0';
}

bool UserProfileStore::isPlausibleName(const char* candidate) {
    if (!candidate || !candidate[0]) return false;
    size_t len = strlen(candidate);
    if (len < 2 || len >= AMELTECH_PROFILE_NAME_LEN) return false;

    // Split into words and validate each one.
    char work[AMELTECH_PROFILE_NAME_LEN];
    strncpy(work, candidate, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    int words = 0;
    char* save = work;
    for (char* p = work;; ++p) {
        if (*p == ' ' || *p == '\0') {
            char keep = *p;
            *p = '\0';
            if (*save) {
                ++words;
                if (words > 3) return false;
                size_t wl = strlen(save);
                if (wl < 1 || wl > 20) return false;
                if (!isAlphaWord(save)) return false;

                char lower[24];
                size_t k = 0;
                for (const char* q = save; *q && k < sizeof(lower) - 1; ++q) {
                    lower[k++] = (char)tolower((unsigned char)*q);
                }
                lower[k] = '\0';
                if (inList(NOT_A_NAME, lower)) return false;
                if (inList(FIELD_WORDS, lower)) return false;
            }
            if (keep == '\0') break;
            save = p + 1;
        }
    }
    if (words == 0) return false;
    // A single one-letter "name" is almost always a false positive.
    if (words == 1 && strlen(work) < 2) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Extraction
// ---------------------------------------------------------------------------
struct NameTrigger {
    const char* phrase;
    bool allowFieldWords;   // "i am a student" is a field, not a name
};

static const NameTrigger NAME_TRIGGERS[] = {
    {"my name is", true},
    {"my name s", true},
    {"my name", true},
    {"name is", true},
    {"i am called", true},
    {"you can call me", true},
    {"you may call me", true},
    {"just call me", true},
    {"call me", true},
    {"this is", true},
    {"myself", true},
    {"i am", false},
    {"i m", false},
    {"im", false},
    {nullptr, false}
};

// Tokens that end a name.
static bool nameStopWord(const char* w) {
    static const char* const stops[] = {
        "and", "but", "i", "im", "is", "was", "the", "a", "an", "from",
        "who", "what", "how", "why", "when", "where", "please", "can",
        "you", "your", "me", "my", "we", "he", "she", "they", "it",
        "here", "there", "now", "today", "also", "too", "very", "so",
        nullptr
    };
    return inList(stops, w);
}

bool UserProfileStore::extractName(const char* rawText, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!rawText) return false;

    // Work on a normalized copy so punctuation and case cannot interfere.
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(rawText, norm, sizeof(norm));
    if (!norm[0]) return false;

    for (int i = 0; NAME_TRIGGERS[i].phrase; ++i) {
        const char* phrase = NAME_TRIGGERS[i].phrase;
        size_t pl = strlen(phrase);

        const char* hit = norm;
        while ((hit = strstr(hit, phrase)) != nullptr) {
            bool leftOk = (hit == norm) || (*(hit - 1) == ' ');
            char after = *(hit + pl);
            bool rightOk = (after == ' ');
            if (!leftOk || !rightOk) { ++hit; continue; }

            const char* p = hit + pl;
            while (*p == ' ') ++p;

            char candidate[AMELTECH_PROFILE_NAME_LEN];
            size_t cl = 0;
            int words = 0;

            while (*p && words < 3) {
                char word[24];
                size_t wl = 0;
                while (*p && *p != ' ' && wl < sizeof(word) - 1) word[wl++] = *p++;
                word[wl] = '\0';
                while (*p == ' ') ++p;
                if (!wl) break;

                if (nameStopWord(word)) break;
                if (inList(FIELD_WORDS, word)) break;
                if (inList(NOT_A_NAME, word)) break;
                if (!isAlphaWord(word)) break;

                if (cl + wl + (cl ? 1 : 0) >= sizeof(candidate)) break;
                if (cl) candidate[cl++] = ' ';
                memcpy(candidate + cl, word, wl);
                cl += wl;
                candidate[cl] = '\0';
                ++words;
            }

            if (cl > 0 && isPlausibleName(candidate)) {
                titleCase(candidate, out, outSize);
                return true;
            }
            ++hit;
        }
    }
    return false;
}

bool UserProfileStore::extractField(const char* rawText, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!rawText) return false;

    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(rawText, norm, sizeof(norm));
    if (!norm[0]) return false;

    static const char* const FIELD_TRIGGERS[] = {
        "i am studying", "i am working as", "i work as", "i am doing",
        "i study", "i am an", "i am a", "i m an", "i m a", "im an", "im a",
        "i am", nullptr
    };

    for (int i = 0; FIELD_TRIGGERS[i]; ++i) {
        const char* phrase = FIELD_TRIGGERS[i];
        size_t pl = strlen(phrase);
        const char* hit = norm;
        while ((hit = strstr(hit, phrase)) != nullptr) {
            bool leftOk = (hit == norm) || (*(hit - 1) == ' ');
            char after = *(hit + pl);
            if (!leftOk || after != ' ') { ++hit; continue; }

            const char* p = hit + pl;
            while (*p == ' ') ++p;

            char candidate[AMELTECH_PROFILE_FIELD_LEN];
            size_t cl = 0;
            int words = 0;
            bool sawFieldWord = false;

            while (*p && words < 4) {
                char word[24];
                size_t wl = 0;
                while (*p && *p != ' ' && wl < sizeof(word) - 1) word[wl++] = *p++;
                word[wl] = '\0';
                while (*p == ' ') ++p;
                if (!wl) break;
                if (strcmp(word, "and") == 0 || strcmp(word, "but") == 0) break;

                if (inList(FIELD_WORDS, word)) sawFieldWord = true;
                if (cl + wl + (cl ? 1 : 0) >= sizeof(candidate)) break;
                if (cl) candidate[cl++] = ' ';
                memcpy(candidate + cl, word, wl);
                cl += wl;
                candidate[cl] = '\0';
                ++words;
            }

            // Only accept it when an occupation word actually appeared,
            // otherwise "i am here" would be stored as a profession.
            if (sawFieldWord && cl > 0) {
                strncpy(out, candidate, outSize - 1);
                out[outSize - 1] = '\0';
                return true;
            }
            ++hit;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
int UserProfileStore::findByName(const char* name) const {
    if (!name || !name[0]) return -1;
    char want[AMELTECH_PROFILE_NAME_LEN];
    titleCase(name, want, sizeof(want));
    for (uint8_t i = 0; i < AMELTECH_MAX_PROFILES; ++i) {
        if (!_entries[i].used) continue;
        if (strcasecmp(_entries[i].name, want) == 0) return (int)i;
    }
    return -1;
}

int UserProfileStore::evictOldest() {
    int oldest = -1;
    uint32_t oldestSeq = 0xFFFFFFFFUL;
    for (uint8_t i = 0; i < AMELTECH_MAX_PROFILES; ++i) {
        if (!_entries[i].used) continue;
        if (_entries[i].lastSeenSeq < oldestSeq) {
            oldestSeq = _entries[i].lastSeenSeq;
            oldest = (int)i;
        }
    }
    if (oldest >= 0) {
        AmelTechLogger.log(AMELTECH_LOG_INFO, "profile evicted: %s", _entries[oldest].name);
        memset(&_entries[oldest], 0, sizeof(UserProfileEntry));
        if (_count) --_count;
    }
    return oldest;
}

int UserProfileStore::addOrTouch(const char* name, const char* field) {
    if (!name || !name[0]) return -1;
    char clean[AMELTECH_PROFILE_NAME_LEN];
    titleCase(name, clean, sizeof(clean));
    if (!isPlausibleName(clean)) return -1;

    int existing = findByName(clean);
    if (existing >= 0) {
        touch(existing);
        if (field && field[0]) {
            strncpy(_entries[existing].field, field, AMELTECH_PROFILE_FIELD_LEN - 1);
            _entries[existing].field[AMELTECH_PROFILE_FIELD_LEN - 1] = '\0';
            _dirty = true;
        }
        return existing;
    }

    int slot = -1;
    for (uint8_t i = 0; i < AMELTECH_MAX_PROFILES; ++i) {
        if (!_entries[i].used) { slot = (int)i; break; }
    }
    // Full: the least recently seen of the 34 makes way for the new one.
    if (slot < 0) slot = evictOldest();
    if (slot < 0) return -1;

    memset(&_entries[slot], 0, sizeof(UserProfileEntry));
    strncpy(_entries[slot].name, clean, AMELTECH_PROFILE_NAME_LEN - 1);
    if (field && field[0]) {
        strncpy(_entries[slot].field, field, AMELTECH_PROFILE_FIELD_LEN - 1);
    }
    _entries[slot].used = true;
    _entries[slot].lastSeenSeq = _seq++;
    _entries[slot].interactions = 1;
    ++_count;
    _dirty = true;
    AmelTechLogger.log(AMELTECH_LOG_INFO, "profile saved: %s (%u/%u)",
                       clean, (unsigned)_count, (unsigned)AMELTECH_MAX_PROFILES);
    return slot;
}

bool UserProfileStore::setField(const char* name, const char* field) {
    int slot = findByName(name);
    if (slot < 0) return false;
    if (!field) return false;
    strncpy(_entries[slot].field, field, AMELTECH_PROFILE_FIELD_LEN - 1);
    _entries[slot].field[AMELTECH_PROFILE_FIELD_LEN - 1] = '\0';
    _dirty = true;
    return true;
}

bool UserProfileStore::removeByName(const char* name) {
    int slot = findByName(name);
    if (slot < 0) return false;
    memset(&_entries[slot], 0, sizeof(UserProfileEntry));
    if (_count) --_count;
    _dirty = true;
    return true;
}

void UserProfileStore::touch(int slot) {
    if (slot < 0 || slot >= AMELTECH_MAX_PROFILES) return;
    if (!_entries[slot].used) return;
    _entries[slot].lastSeenSeq = _seq++;
    if (_entries[slot].interactions < 0xFFFF) ++_entries[slot].interactions;
    _dirty = true;
}

const UserProfileEntry* UserProfileStore::at(int slot) const {
    if (slot < 0 || slot >= AMELTECH_MAX_PROFILES) return nullptr;
    if (!_entries[slot].used) return nullptr;
    return &_entries[slot];
}

int UserProfileStore::slotByRecency(uint8_t rank) const {
    // Selection scan: at most 34 entries, so a full sort is not worth it.
    uint32_t previousSeq = 0xFFFFFFFFUL;
    int chosen = -1;
    for (uint8_t r = 0; r <= rank; ++r) {
        chosen = -1;
        uint32_t bestSeq = 0;
        for (uint8_t i = 0; i < AMELTECH_MAX_PROFILES; ++i) {
            if (!_entries[i].used) continue;
            uint32_t s = _entries[i].lastSeenSeq;
            if (s >= previousSeq) continue;
            if (chosen < 0 || s > bestSeq) {
                bestSeq = s;
                chosen = (int)i;
            }
        }
        if (chosen < 0) return -1;
        previousSeq = bestSeq;
    }
    return chosen;
}

const UserProfileEntry* UserProfileStore::byRecency(uint8_t rank) const {
    return at(slotByRecency(rank));
}

String UserProfileStore::list() const {
    String out;
    out.reserve(64 + (size_t)_count * 48);
    out += F("Remembered names: ");
    out += (int)_count;
    out += '/';
    out += (int)AMELTECH_MAX_PROFILES;
    out += '\n';
    for (uint8_t r = 0; r < _count; ++r) {
        const UserProfileEntry* e = byRecency(r);
        if (!e) break;
        out += (int)(r + 1);
        out += F(". ");
        out += e->name;
        if (e->field[0]) {
            out += F(" - ");
            out += e->field;
        }
        out += F(" (");
        out += (int)e->interactions;
        out += F(" chats)\n");
    }
    return out;
}

// ---------------------------------------------------------------------------
int8_t UserProfileStore::save() {
#if AMELTECH_NVS_AVAILABLE
    Preferences prefs;
    if (!prefs.begin(AMELTECH_NVS_ID, false)) return -1;

    uint8_t previous = prefs.getUChar("cnt", 0);
    uint8_t written = 0;
    for (uint8_t r = 0; r < AMELTECH_MAX_PROFILES; ++r) {
        // Persist in recency order so a truncated load keeps the newest.
        const UserProfileEntry* e = byRecency(r);
        if (!e) break;
        char kn[16], kf[16], ki[16];
        snprintf(kn, sizeof(kn), "n%u", (unsigned)written);
        snprintf(kf, sizeof(kf), "f%u", (unsigned)written);
        snprintf(ki, sizeof(ki), "i%u", (unsigned)written);
        prefs.putString(kn, e->name);
        prefs.putString(kf, e->field);
        prefs.putUShort(ki, e->interactions);
        ++written;
    }
    for (uint8_t i = written; i < previous; ++i) {
        char kn[16], kf[16], ki[16];
        unsigned idx = (unsigned)(i & 0xFF);
        snprintf(kn, sizeof(kn), "n%u", idx);
        snprintf(kf, sizeof(kf), "f%u", idx);
        snprintf(ki, sizeof(ki), "i%u", idx);
        prefs.remove(kn);
        prefs.remove(kf);
        prefs.remove(ki);
    }
    prefs.putUChar("cnt", written);
    prefs.end();
    _dirty = false;
    return 0;
#else
    _dirty = false;
    return -1;
#endif
}

int8_t UserProfileStore::load() {
#if AMELTECH_NVS_AVAILABLE
    Preferences prefs;
    if (!prefs.begin(AMELTECH_NVS_ID, true)) return -1;
    uint8_t count = prefs.getUChar("cnt", 0);
    if (count > AMELTECH_MAX_PROFILES) count = AMELTECH_MAX_PROFILES;

    clear();
    // Stored newest first, so insert in reverse to rebuild the recency order.
    for (int i = (int)count - 1; i >= 0; --i) {
        char kn[16], kf[16], ki[16];
        snprintf(kn, sizeof(kn), "n%u", (unsigned)i);
        snprintf(kf, sizeof(kf), "f%u", (unsigned)i);
        snprintf(ki, sizeof(ki), "i%u", (unsigned)i);
        String n = prefs.getString(kn, "");
        String f = prefs.getString(kf, "");
        uint16_t inter = prefs.getUShort(ki, 1);
        if (n.length() == 0) continue;
        int slot = addOrTouch(n.c_str(), f.c_str());
        if (slot >= 0) _entries[slot].interactions = inter;
    }
    prefs.end();
    _dirty = false;
    AmelTechLogger.log(AMELTECH_LOG_INFO, "profiles loaded n=%u", (unsigned)_count);
    return 0;
#else
    return -1;
#endif
}

// ===========================================================================
// IdentityManager
// ===========================================================================
IdentityManager::IdentityManager()
    : _store(nullptr),
      _activeSlot(-1),
      _state(S_IDLE),
      _guessRank(0),
      _rejections(0),
      _confusions(0),
      _nameAsks(0),
      _bootPending(false),
      _repliesSinceName(0),
      _greetNextReply(false) {
    _deferred[0] = '\0';
}

void IdentityManager::begin(UserProfileStore* store) {
    _store = store;
    reset();
    onBoot();
}

void IdentityManager::reset() {
    _activeSlot = -1;
    _state = S_IDLE;
    _guessRank = 0;
    _rejections = 0;
    _confusions = 0;
    _nameAsks = 0;
    _bootPending = false;
    _repliesSinceName = 0;
    _greetNextReply = false;
    _deferred[0] = '\0';
}

void IdentityManager::onBoot() {
    // After a reset or power cycle the bot does not assume who is talking.
    // If it remembers anyone, the first message triggers a confirmation.
    _activeSlot = -1;
    _state = S_IDLE;
    _guessRank = 0;
    _rejections = 0;
    _confusions = 0;
    _nameAsks = 0;
    _bootPending = (_store && _store->count() > 0);
}

const char* IdentityManager::activeName() const {
    if (!_store || _activeSlot < 0) return nullptr;
    const UserProfileEntry* e = _store->at(_activeSlot);
    return e ? e->name : nullptr;
}

const char* IdentityManager::activeField() const {
    if (!_store || _activeSlot < 0) return nullptr;
    const UserProfileEntry* e = _store->at(_activeSlot);
    return (e && e->field[0]) ? e->field : nullptr;
}

// ---------------------------------------------------------------------------
static const char* skipLeadingWord(const char* s) {
    while (*s == ' ') ++s;
    while (*s && *s != ' ') ++s;
    while (*s == ' ' || *s == ',' || *s == '.' || *s == '!') ++s;
    return s;
}

bool IdentityManager::isAffirmative(const char* text, const char** remainder) {
    if (!text) return false;
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(text, norm, sizeof(norm));
    if (!norm[0]) return false;

    static const char* const YES_ONE[] = {
        "yes", "y", "yeah", "yep", "yup", "ya", "yea", "correct", "right",
        "true", "affirmative", "ok", "okay", "sure", "indeed", "exactly",
        nullptr
    };
    static const char* const YES_PHRASE[] = {
        "that is me", "thats me", "that s me", "i am", "it is me", "its me",
        "yes it is", "yes i am", "you are right", "you are correct",
        nullptr
    };

    char first[24];
    size_t i = 0;
    const char* p = norm;
    while (*p && *p != ' ' && i < sizeof(first) - 1) first[i++] = *p++;
    first[i] = '\0';

    if (inList(YES_ONE, first)) {
        if (remainder) *remainder = skipLeadingWord(text);
        return true;
    }
    for (int k = 0; YES_PHRASE[k]; ++k) {
        size_t pl = strlen(YES_PHRASE[k]);
        if (strncmp(norm, YES_PHRASE[k], pl) == 0 &&
            (norm[pl] == '\0' || norm[pl] == ' ')) {
            if (remainder) *remainder = "";
            return true;
        }
    }
    return false;
}

bool IdentityManager::isNegative(const char* text, const char** remainder) {
    if (!text) return false;
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(text, norm, sizeof(norm));
    if (!norm[0]) return false;

    static const char* const NO_ONE[] = {
        "no", "n", "nope", "nah", "negative", "wrong", "incorrect", "false",
        "never", nullptr
    };
    static const char* const NO_PHRASE[] = {
        "not me", "that is not me", "thats not me", "that s not me",
        "i am not", "no i am not", "you are wrong", nullptr
    };

    char first[24];
    size_t i = 0;
    const char* p = norm;
    while (*p && *p != ' ' && i < sizeof(first) - 1) first[i++] = *p++;
    first[i] = '\0';

    if (inList(NO_ONE, first)) {
        if (remainder) *remainder = skipLeadingWord(text);
        return true;
    }
    for (int k = 0; NO_PHRASE[k]; ++k) {
        size_t pl = strlen(NO_PHRASE[k]);
        if (strncmp(norm, NO_PHRASE[k], pl) == 0 &&
            (norm[pl] == '\0' || norm[pl] == ' ')) {
            if (remainder) *remainder = "";
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
bool IdentityManager::offerNextGuess(String& reply) {
    if (!_store) return false;
    const UserProfileEntry* e = _store->byRecency(_guessRank);
    if (!e) return false;

    reply = F("Are you ");
    reply += e->name;
    reply += F("?");
    _state = S_CONFIRM;
    return true;
}

void IdentityManager::confirmSlot(int slot) {
    _activeSlot = slot;
    if (_store) _store->touch(slot);
    _state = S_IDLE;
    _rejections = 0;
    _confusions = 0;
    _guessRank = 0;
    _bootPending = false;
    _greetNextReply = true;
    _repliesSinceName = 0;
}

bool IdentityManager::captureIntroduction(const String& raw, String& acknowledgement,
                                          String& leftover) {
    if (!_store) return false;

    char name[AMELTECH_PROFILE_NAME_LEN];
    if (!UserProfileStore::extractName(raw.c_str(), name, sizeof(name))) return false;

    char field[AMELTECH_PROFILE_FIELD_LEN];
    bool haveField = UserProfileStore::extractField(raw.c_str(), field, sizeof(field));

    bool wasKnown = (_store->findByName(name) >= 0);
    int slot = _store->addOrTouch(name, haveField ? field : nullptr);
    if (slot < 0) return false;

    confirmSlot(slot);

    acknowledgement = wasKnown ? F("Welcome back, ") : F("Nice to meet you, ");
    acknowledgement += name;
    acknowledgement += F(".");
    if (haveField) {
        acknowledgement += F(" I have saved that you are ");
        // "an engineering student" reads better than "engineering student".
        char c = field[0];
        bool vowel = (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u');
        acknowledgement += vowel ? F("an ") : F("a ");
        acknowledgement += field;
        acknowledgement += F(".");
    } else if (!wasKnown) {
        acknowledgement += F(" I will remember your name.");
    }

    leftover = "";
    return true;
}

// ---------------------------------------------------------------------------
IdentityAction IdentityManager::process(const String& raw, String& reply, String& question) {
    reply = "";
    question = "";
    if (!_store) return ID_ACTION_NONE;

    // ---- 1. First message after a reset --------------------------------
    if (_bootPending && _state == S_IDLE) {
        _bootPending = false;

        // An explicit introduction beats guessing.
        String ack, leftover;
        if (captureIntroduction(raw, ack, leftover)) {
            reply = ack;
            question = leftover;
            return leftover.length() ? ID_ACTION_CONTINUE_WITH : ID_ACTION_REPLY;
        }

        _guessRank = 0;
        _rejections = 0;
        if (offerNextGuess(reply)) {
            // Remember what they asked so it can be answered after the
            // identity question is settled.
            strncpy(_deferred, raw.c_str(), sizeof(_deferred) - 1);
            _deferred[sizeof(_deferred) - 1] = '\0';
            return ID_ACTION_REPLY;
        }
    }

    // ---- 2. Waiting for yes / no ---------------------------------------
    if (_state == S_CONFIRM) {
        const char* rest = nullptr;

        if (isAffirmative(raw.c_str(), &rest)) {
            int slot = _store->slotByRecency(_guessRank);
            if (slot < 0) {
                _state = S_AWAIT_NAME;
                reply = F("How are you,.. What is your name?");
                return ID_ACTION_REPLY;
            }
            confirmSlot(slot);
            const UserProfileEntry* e = _store->at(slot);

            reply = F("Good to see you again, ");
            reply += e ? e->name : "friend";
            reply += F(".");
            if (e && e->field[0]) {
                char c0 = e->field[0];
                bool vowel = (c0 == 'a' || c0 == 'e' || c0 == 'i' ||
                              c0 == 'o' || c0 == 'u');
                reply += F(" Still ");
                reply += vowel ? F("an ") : F("a ");
                reply += e->field;
                reply += F("?");
            }

            // Answer whatever came after the "yes", or the question that was
            // interrupted by the identity check.
            String pending;
            if (rest && rest[0]) pending = rest;
            else if (_deferred[0]) pending = _deferred;
            _deferred[0] = '\0';

            if (pending.length() > 0) {
                question = pending;
                return ID_ACTION_CONTINUE_WITH;
            }
            return ID_ACTION_REPLY;
        }

        if (isNegative(raw.c_str(), &rest)) {
            ++_rejections;
            _confusions = 0;

            // Four rejections is enough guessing.
            if (_rejections >= AMELTECH_IDENTITY_MAX_GUESSES) {
                _state = S_AWAIT_NAME;
                _nameAsks = 1;
                reply = F("How are you,.. What is your name?");
                return ID_ACTION_REPLY;
            }

            ++_guessRank;
            if (offerNextGuess(reply)) return ID_ACTION_REPLY;

            // Nothing left to guess.
            _state = S_AWAIT_NAME;
            _nameAsks = 1;
            reply = F("How are you,.. What is your name?");
            return ID_ACTION_REPLY;
        }

        // Not a yes or a no. Perhaps they introduced themselves instead.
        String ack, leftover;
        if (captureIntroduction(raw, ack, leftover)) {
            reply = ack;
            String pending = _deferred[0] ? String(_deferred) : String("");
            _deferred[0] = '\0';
            if (pending.length()) {
                question = pending;
                return ID_ACTION_CONTINUE_WITH;
            }
            return ID_ACTION_REPLY;
        }

        // Ask once more, then stop pestering and just answer the question.
        ++_confusions;
        if (_confusions <= 1) {
            const UserProfileEntry* e = _store->byRecency(_guessRank);
            reply = F("Sorry, I did not catch that. Are you ");
            reply += e ? e->name : "there";
            reply += F("? Please answer yes or no.");
            return ID_ACTION_REPLY;
        }

        _state = S_IDLE;
        _confusions = 0;
        _deferred[0] = '\0';
        return ID_ACTION_NONE;
    }

    // ---- 3. Waiting for a name -----------------------------------------
    if (_state == S_AWAIT_NAME) {
        String ack, leftover;
        if (captureIntroduction(raw, ack, leftover)) {
            reply = ack;
            String pending = _deferred[0] ? String(_deferred) : String("");
            _deferred[0] = '\0';
            if (pending.length()) {
                question = pending;
                return ID_ACTION_CONTINUE_WITH;
            }
            return ID_ACTION_REPLY;
        }

        // A bare name with no introducing phrase, e.g. just "Arjun".
        char bare[AMELTECH_PROFILE_NAME_LEN];
        AmelTechText::copyTrimmed(raw.c_str(), bare, sizeof(bare));
        if (UserProfileStore::isPlausibleName(bare)) {
            int slot = _store->addOrTouch(bare, nullptr);
            if (slot >= 0) {
                confirmSlot(slot);
                const UserProfileEntry* e = _store->at(slot);
                reply = F("Nice to meet you, ");
                reply += e ? e->name : bare;
                reply += F(". I will remember your name.");

                String pending = _deferred[0] ? String(_deferred) : String("");
                _deferred[0] = '\0';
                if (pending.length()) {
                    question = pending;
                    return ID_ACTION_CONTINUE_WITH;
                }
                return ID_ACTION_REPLY;
            }
        }

        ++_nameAsks;
        if (_nameAsks <= 2) {
            reply = F("I still do not have your name. You can say: my name is Arjun.");
            return ID_ACTION_REPLY;
        }

        // Do not block the conversation forever.
        _state = S_IDLE;
        _nameAsks = 0;
        _deferred[0] = '\0';
        return ID_ACTION_NONE;
    }

    // ---- 4. Normal conversation ----------------------------------------
    String ack, leftover;
    if (captureIntroduction(raw, ack, leftover)) {
        reply = ack;
        return ID_ACTION_REPLY;
    }

    // "what is my name" is handled here because only this module knows.
    char norm[AMELTECH_MAX_QUESTION_LEN];
    AmelTechText::normalize(raw.c_str(), norm, sizeof(norm));
    if (strstr(norm, "what is my name") || strstr(norm, "do you know my name") ||
        strstr(norm, "who am i") || strstr(norm, "do you remember me")) {
        const char* n = activeName();
        if (n) {
            reply = F("You are ");
            reply += n;
            const char* f = activeField();
            if (f) {
                reply += F(", and you told me you are ");
                char c0 = f[0];
                bool vowel = (c0 == 'a' || c0 == 'e' || c0 == 'i' ||
                              c0 == 'o' || c0 == 'u');
                reply += vowel ? F("an ") : F("a ");
                reply += f;
            }
            reply += F(".");
        } else if (_store->count() > 0) {
            _guessRank = 0;
            _rejections = 0;
            offerNextGuess(reply);
            if (reply.length() == 0) {
                reply = F("I do not know yet. What is your name?");
                _state = S_AWAIT_NAME;
            }
        } else {
            reply = F("I do not know your name yet. What is your name?");
            _state = S_AWAIT_NAME;
            _nameAsks = 1;
        }
        return ID_ACTION_REPLY;
    }

    return ID_ACTION_NONE;
}

bool IdentityManager::shouldMentionName() {
    if (_activeSlot < 0) return false;
    if (_greetNextReply) return true;
    return (_repliesSinceName >= AMELTECH_NAME_MENTION_GAP);
}

void IdentityManager::noteReply() {
    if (_greetNextReply) {
        _greetNextReply = false;
        _repliesSinceName = 0;
        return;
    }
    if (_repliesSinceName >= AMELTECH_NAME_MENTION_GAP) _repliesSinceName = 0;
    else ++_repliesSinceName;
}
