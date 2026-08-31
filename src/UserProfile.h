/*
 * UserProfile.h
 * ---------------------------------------------------------------------------
 * Name memory and the identity conversation.
 *
 * Behaviour
 *   - Saying "hi my name is Joky Pk" stores the name automatically.
 *   - Saying "I am an engineering student" stores the field automatically.
 *   - After a reset or a power cycle the first message of the session is
 *     answered with "Are you Joky Pk?" using the most recently seen name.
 *   - "yes" (with or without a question attached) confirms it and the bot then
 *     answers whatever was asked, addressing the person by name.
 *   - "no" moves on to the next remembered name. After four rejections the bot
 *     stops guessing and asks "How are you,.. What is your name?" instead.
 *   - Answering that with "My name is Arjun. I'm an engineering student" saves
 *     both the name and the field if they are not already stored.
 *   - At most 34 names are kept. Saving a new one when full removes the 34th,
 *     that is the least recently seen.
 *
 * Everything is stored in NVS, so it survives a reset and a power cycle.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_USER_PROFILE_H
#define AMELTECH_USER_PROFILE_H

#include <Arduino.h>
#include "AmelTechConfig.h"

struct UserProfileEntry {
    char name[AMELTECH_PROFILE_NAME_LEN];
    char field[AMELTECH_PROFILE_FIELD_LEN];
    uint32_t lastSeenSeq;     // higher is more recent
    uint16_t interactions;
    bool used;
};

// What the caller should do with the result of IdentityManager::process().
enum IdentityAction : uint8_t {
    ID_ACTION_NONE = 0,       // nothing identity related happened
    ID_ACTION_REPLY,          // `reply` is the complete answer
    ID_ACTION_CONTINUE_WITH   // `reply` is a prefix; answer `question` next
};

// ---------------------------------------------------------------------------
class UserProfileStore {
public:
    UserProfileStore();

    bool begin();
    void clear();

    // Adds the name, or refreshes it if already known. `field` may be null or
    // empty, in which case an existing field is preserved.
    // Returns the slot index, or -1 if the name was rejected.
    int addOrTouch(const char* name, const char* field);

    int findByName(const char* name) const;
    bool setField(const char* name, const char* field);
    bool removeByName(const char* name);

    const UserProfileEntry* at(int slot) const;
    // rank 0 is the most recently seen profile.
    const UserProfileEntry* byRecency(uint8_t rank) const;
    int slotByRecency(uint8_t rank) const;

    uint8_t count() const { return _count; }
    static uint8_t capacity() { return AMELTECH_MAX_PROFILES; }
    bool isFull() const { return _count >= AMELTECH_MAX_PROFILES; }

    void touch(int slot);
    bool isDirty() const { return _dirty; }

    int8_t save();
    int8_t load();

    String list() const;

    // Name handling helpers, public because the bot and the tests use them.
    static bool isPlausibleName(const char* candidate);
    static void titleCase(const char* in, char* out, size_t outSize);
    static bool extractName(const char* rawText, char* out, size_t outSize);
    static bool extractField(const char* rawText, char* out, size_t outSize);

private:
    UserProfileEntry _entries[AMELTECH_MAX_PROFILES];
    uint8_t _count;
    uint32_t _seq;
    bool _dirty;

    int evictOldest();
};

// ---------------------------------------------------------------------------
class IdentityManager {
public:
    IdentityManager();

    void begin(UserProfileStore* store);

    // Arms the "Are you X?" question for the first message after a reset.
    void onBoot();

    // Runs before the normal answer pipeline.
    //   raw        the user's message as typed
    //   reply      filled according to the returned action
    //   question   filled with the text still to be answered, when the action
    //              is ID_ACTION_CONTINUE_WITH
    IdentityAction process(const String& raw, String& reply, String& question);

    bool hasActiveUser() const { return _activeSlot >= 0; }
    const char* activeName() const;
    const char* activeField() const;

    // True when this reply should address the user by name. The bot greets by
    // name immediately after confirming, then every AMELTECH_NAME_MENTION_GAP
    // replies, so it stays personal without repeating itself every turn.
    bool shouldMentionName();
    void noteReply();

    void forgetActive() { _activeSlot = -1; }
    void reset();

    static bool isAffirmative(const char* text, const char** remainder);
    static bool isNegative(const char* text, const char** remainder);

private:
    UserProfileStore* _store;
    int _activeSlot;

    enum State : uint8_t { S_IDLE = 0, S_CONFIRM, S_AWAIT_NAME } _state;

    uint8_t _guessRank;       // which remembered name is being offered
    uint8_t _rejections;      // consecutive "no" answers
    uint8_t _confusions;      // consecutive unparseable answers
    uint8_t _nameAsks;        // how many times the open question was asked
    bool _bootPending;
    uint8_t _repliesSinceName;
    bool _greetNextReply;
    char _deferred[AMELTECH_MAX_QUESTION_LEN];

    bool offerNextGuess(String& reply);
    void confirmSlot(int slot);
    bool captureIntroduction(const String& raw, String& acknowledgement, String& leftover);
};

#endif // AMELTECH_USER_PROFILE_H
