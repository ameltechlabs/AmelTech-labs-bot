#include "TrainingConsole.h"
#include "AmelTechLog.h"
#include "NeuralEngine.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

TrainingConsole::TrainingConsole()
    : _kb(nullptr),
      _accepted(0),
      _rejected(0),
      _lastCode(0),
      _minFreeHeap(AMELTECH_TRAIN_MIN_FREE_HEAP) {}

void TrainingConsole::begin(KnowledgeBase* kb) {
    _kb = kb;
    if (_kb) _kb->setMinFreeHeap(_minFreeHeap);
}

void TrainingConsole::setMinFreeHeap(uint32_t bytes) {
    _minFreeHeap = bytes;
    if (_kb) _kb->setMinFreeHeap(bytes);
}

uint32_t TrainingConsole::freeHeapBytes() {
#if defined(ESP32)
    return (uint32_t)ESP.getFreeHeap();
#else
    return 0xFFFFFFFFUL;   // host builds are not heap limited
#endif
}

// ---------------------------------------------------------------------------
static void trimInPlace(String& s) {
    while (s.length() && (s[0] == ' ' || s[0] == '\t')) s.remove(0, 1);
    while (s.length()) {
        char c = s[s.length() - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') s.remove(s.length() - 1);
        else break;
    }
}

static String lowerCopy(const String& s) {
    String out = s;
    out.toLowerCase();
    return out;
}

bool TrainingConsole::isTrainingCommand(const char* line) {
    if (!line) return false;
    while (*line == ' ' || *line == '\t') ++line;

    static const char kw[] = "train";
    for (int i = 0; i < 5; ++i) {
        if (tolower((unsigned char)line[i]) != kw[i]) return false;
    }
    char next = line[5];
    // "train | ..." or "train|..." or "training". Anything else is chat.
    return (next == '\0' || next == ' ' || next == '|' || next == '\t');
}

String TrainingConsole::helpText() {
    String s;
    s.reserve(420);
    s += F("Training commands\n");
    s += F("  train | <question> | <answer>   teach something new\n");
    s += F("  train | delete | 0001           delete one entry by code\n");
    s += F("  train | delete | full data      delete everything taught\n");
    s += F("  train | list                    list taught entries\n");
    s += F("  train | status                  memory and capacity\n");
    s += F("  train | save                    write to flash now\n");
    s += F("  train | help                    this message\n");
    s += F("Example: train | who made you | AmelTech labs made me.");
    return s;
}

String TrainingConsole::statusReport() const {
    String s;
    s.reserve(340);
    uint32_t heap = freeHeapBytes();

    s += F("Training status\n");
    s += F("  Taught entries : ");
    s += (int)(_kb ? _kb->userCount() : 0);
    s += '/';
    s += (int)AMELTECH_MAX_USER_ENTRIES;
    s += '\n';
    s += F("  Next data code : ");
    char code[8];
    snprintf(code, sizeof(code), "%04u", (unsigned)(_kb ? _kb->peekNextCode() : 1));
    s += code;
    s += '\n';
    s += F("  Free heap      : ");
    if (heap == 0xFFFFFFFFUL) s += F("unlimited (host build)");
    else {
        s += (int)(heap / 1024);
        s += F(" KB");
    }
    s += '\n';
    s += F("  Reserved heap  : ");
    s += (int)(_minFreeHeap / 1024);
    s += F(" KB kept free for chat logging\n");
    s += F("  Training open  : ");
    s += (heap > _minFreeHeap) ? F("yes") : F("no, memory reserve reached");
    s += '\n';
    s += F("  Accepted / rejected lessons: ");
    s += (int)_accepted;
    s += F(" / ");
    s += (int)_rejected;
    return s;
}

// ---------------------------------------------------------------------------
String TrainingConsole::describeAddError(int8_t code, const String& question) const {
    String s;
    switch (code) {
        case KB_ADD_DUPLICATE:
            s = F("That question is already taught with the same answer, so nothing changed.");
            break;
        case KB_ADD_CONFLICT:
            s = F("A taught entry already answers \"");
            s += question;
            s += F("\". It has been updated with your new answer.");
            break;
        case KB_ADD_INVALID:
            s = F("Both the question and the answer must contain real text. "
                  "Use: train | question | answer");
            break;
        case KB_ADD_TOO_LARGE:
            s = F("Too long. Keep the question under ");
            s += (int)(AMELTECH_MAX_QUESTION_LEN - 1);
            s += F(" characters and the answer under ");
            s += (int)(AMELTECH_MAX_ANSWER_LEN - 1);
            s += F(" characters.");
            break;
        case KB_ADD_FULL:
            s = F("Training memory is full (");
            s += (int)AMELTECH_MAX_USER_ENTRIES;
            s += F(" entries). Delete one with: train | delete | <code>");
            break;
        case KB_ADD_NO_MEMORY:
            s = F("Not enough free memory to store that lesson right now.");
            break;
        case KB_ADD_HEAP_GUARD: {
            uint32_t heap = freeHeapBytes();
            s = F("Training is paused: only ");
            s += (int)(heap / 1024);
            s += F(" KB of heap is free and ");
            s += (int)(_minFreeHeap / 1024);
            s += F(" KB is reserved for chat logging. "
                   "Free some memory or delete an entry, then try again.");
            break;
        }
        default:
            s = F("Training failed for an unknown reason.");
            break;
    }
    return s;
}

String TrainingConsole::teach(const String& question, const String& answer,
                              const char* category) {
    if (!_kb) {
        ++_rejected;
        return String(F("Training is not available: knowledge base not started."));
    }

    String q = question;
    String a = answer;
    trimInPlace(q);
    trimInPlace(a);

    if (q.length() == 0 || a.length() == 0) {
        ++_rejected;
        return String(F("Use: train | question | answer  "
                        "(both parts are required)"));
    }

    uint16_t assigned = 0;
    int8_t rc = _kb->addUser(q.c_str(), a.c_str(), category, &assigned);

    if (rc == KB_ADD_OK || rc == KB_ADD_CONFLICT) {
        ++_accepted;
        _lastCode = assigned;
        char code[8];
        snprintf(code, sizeof(code), "%04u", (unsigned)assigned);

        String s;
        s.reserve(96);
        // Exactly the required confirmation wording.
        s += F("train successfully and save data number code ");
        s += code;
        if (rc == KB_ADD_CONFLICT) {
            s += F("\n(the previous answer for that question was replaced)");
        }
        AmelTechLogger.log(AMELTECH_LOG_INFO, "trained code %s", code);
        return s;
    }

    ++_rejected;
    AmelTechLogger.log(AMELTECH_LOG_WARN, "train rejected rc=%d", (int)rc);
    return describeAddError(rc, q);
}

// ---------------------------------------------------------------------------
String TrainingConsole::deleteCommand(const String& target) {
    if (!_kb) return String(F("Knowledge base not started."));

    String t = target;
    trimInPlace(t);
    String lower = lowerCopy(t);

    if (lower == "full data" || lower == "fulldata" || lower == "all" ||
        lower == "full" || lower == "everything" || lower == "full_data") {
        size_t n = _kb->userCount();
        if (n == 0) return String(F("There is nothing taught to delete."));
        _kb->clearUser();
        _kb->saveToNvs();
        String s;
        s += F("train delete successfully, ");
        s += (int)n;
        s += F(" taught ");
        s += (n == 1) ? F("entry") : F("entries");
        s += F(" removed. Built-in knowledge is untouched.");
        AmelTechLogger.log(AMELTECH_LOG_INFO, "training data cleared n=%u", (unsigned)n);
        return s;
    }

    // A 4-digit data number, with or without leading zeros.
    bool numeric = t.length() > 0;
    for (size_t i = 0; i < t.length(); ++i) {
        if (!isdigit((unsigned char)t[i])) { numeric = false; break; }
    }

    if (numeric) {
        long value = t.toInt();
        if (value <= 0 || value > AMELTECH_TRAIN_MAX_CODE) {
            return String(F("Data numbers run from 0001 to 9999."));
        }
        uint16_t code = (uint16_t)value;
        int8_t rc = _kb->removeUserByCode(code);
        char pretty[8];
        snprintf(pretty, sizeof(pretty), "%04u", (unsigned)code);

        if (rc == 0) {
            _kb->saveToNvs();
            String s;
            s += F("train delete successfully data number code ");
            s += pretty;
            AmelTechLogger.log(AMELTECH_LOG_INFO, "deleted code %s", pretty);
            return s;
        }
        String s;
        s += F("No taught entry has data number code ");
        s += pretty;
        s += F(". Use: train | list  to see the codes in use.");
        return s;
    }

    // Otherwise treat it as the question text.
    int8_t rc = _kb->removeUser(t.c_str());
    if (rc == 0) {
        _kb->saveToNvs();
        String s;
        s += F("train delete successfully: \"");
        s += t;
        s += F("\" removed.");
        return s;
    }
    String s;
    s += F("Nothing taught matches \"");
    s += t;
    s += F("\". Delete by code instead, for example: train | delete | 0001");
    return s;
}

// ---------------------------------------------------------------------------
String TrainingConsole::handle(const String& line) {
    if (!_kb) return String(F("Training is not available: knowledge base not started."));

    String work = line;
    trimInPlace(work);

    // Strip the leading keyword.
    int cut = work.indexOf('|');
    String head = (cut < 0) ? work : work.substring(0, cut);
    trimInPlace(head);
    String lowerHead = lowerCopy(head);

    if (cut < 0) {
        // Bare "train" with nothing else.
        return helpText();
    }

    String rest = work.substring(cut + 1);

    // Split the remainder on the next separator.
    int cut2 = rest.indexOf('|');
    String first = (cut2 < 0) ? rest : rest.substring(0, cut2);
    String second = (cut2 < 0) ? String("") : rest.substring(cut2 + 1);
    trimInPlace(first);
    trimInPlace(second);
    String lowerFirst = lowerCopy(first);

    // ---- sub commands ---------------------------------------------------
    if (lowerFirst == "help" || lowerFirst == "?") return helpText();
    if (lowerFirst == "status" || lowerFirst == "info") return statusReport();

    if (lowerFirst == "list" || lowerFirst == "show") {
        size_t n = _kb->userCount();
        if (n == 0) {
            return String(F("Nothing has been taught yet. "
                            "Try: train | who made you | AmelTech labs made me."));
        }
        String s;
        s.reserve(64 + n * 72);
        s += F("Taught entries (");
        s += (int)n;
        s += '/';
        s += (int)AMELTECH_MAX_USER_ENTRIES;
        s += F("):\n");
        for (size_t i = 0; i < n; ++i) {
            const UserKnowledgeEntry* e = _kb->userAt(i);
            if (!e) continue;
            char code[8];
            snprintf(code, sizeof(code), "%04u", (unsigned)e->code);
            s += code;
            s += F("  ");
            s += e->question;
            s += F("  ->  ");
            s += e->answer;
            s += '\n';
        }
        return s;
    }

    if (lowerFirst == "save") {
        int8_t rc = _kb->saveToNvs();
        if (rc == 0) {
            String s;
            s += F("Saved ");
            s += (int)_kb->userCount();
            s += F(" taught entries to flash.");
            return s;
        }
        return String(F("Save failed: flash storage is not available on this build."));
    }

    if (lowerFirst == "delete" || lowerFirst == "remove" || lowerFirst == "del") {
        if (second.length() == 0) {
            return String(F("Tell me what to delete: "
                            "train | delete | 0001   or   train | delete | full data"));
        }
        return deleteCommand(second);
    }

    // ---- teaching -------------------------------------------------------
    if (cut2 < 0) {
        String s;
        s += F("The answer is missing. Use: train | ");
        s += first.length() ? first : String(F("question"));
        s += F(" | answer");
        ++_rejected;
        return s;
    }

    // Guard before touching the knowledge base so the message is specific.
    uint32_t heap = freeHeapBytes();
    if (_minFreeHeap > 0 && heap <= _minFreeHeap) {
        ++_rejected;
        String s;
        s += F("Training is paused. Free heap is ");
        s += (int)(heap / 1024);
        s += F(" KB and ");
        s += (int)(_minFreeHeap / 1024);
        s += F(" KB is reserved so chat logging never runs out of memory.\n"
               "Delete an entry with: train | delete | <code>   then try again.");
        return s;
    }

    return teach(first, second, "user");
}
