/*
 * host_test.cpp
 * ---------------------------------------------------------------------------
 * Desktop test suite for AmelTech lab's bot.
 *
 * Build and run:
 *   cd tests && cmake -B build . && cmake --build build && ./build/ameltech_tests
 *
 * Or directly with g++, from the library root:
 *   g++ -std=c++17 -O1 -DAMELTECH_HOST_NVS -Itests/host_stub -Isrc
 *       tests/host_test.cpp tests/host_stub/host_stub.cpp src (all .cpp) -o t
 *
 * The suite covers the parts that are easy to get wrong and expensive to debug
 * on hardware: normalizer agreement, matcher confidence, calculator semantics,
 * the identity state machine, the training console and the DHT maths.
 * ---------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <Preferences.h>

#include "AmelTechBot.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
static int g_checks = 0;
static int g_failures = 0;
static const char* g_section = "";

static void section(const char* name) {
    g_section = name;
    printf("\n== %s ==\n", name);
}

static void check(bool ok, const char* what) {
    ++g_checks;
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        ++g_failures;
        printf("  FAIL  [%s] %s\n", g_section, what);
    }
}

static void checkStr(const char* got, const char* want, const char* what) {
    bool ok = (got && want && strcmp(got, want) == 0);
    ++g_checks;
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        ++g_failures;
        printf("  FAIL  [%s] %s\n        got  \"%s\"\n        want \"%s\"\n",
               g_section, what, got ? got : "(null)", want ? want : "(null)");
    }
}

static void checkNear(double got, double want, double tol, const char* what) {
    bool ok = std::fabs(got - want) <= tol;
    ++g_checks;
    if (ok) {
        printf("  ok    %s\n", what);
    } else {
        ++g_failures;
        printf("  FAIL  [%s] %s  got %.10g want %.10g\n", g_section, what, got, want);
    }
}

static bool contains(const String& haystack, const char* needle) {
    return haystack.indexOf(needle) >= 0;
}

// ---------------------------------------------------------------------------
static void testNormalizer() {
    section("normalizer");

    char buf[160];

    AmelTechText::normalize("What is Wi-Fi?", buf, sizeof(buf));
    checkStr(buf, "what is wifi", "Wi-Fi collapses to wifi");

    AmelTechText::normalize("what is wifi", buf, sizeof(buf));
    checkStr(buf, "what is wifi", "wifi is left alone");

    // The v1 defect: the runtime and the generator disagreed, so stored rows
    // never matched exactly. Both sides now share this table.
    AmelTechText::normalize("What's an ESP-32 GPIO?", buf, sizeof(buf));
    checkStr(buf, "what is an esp32 gpio", "apostrophes vanish, esp 32 rejoins");

    AmelTechText::normalize("what is ohm's law", buf, sizeof(buf));
    AmelTechText::normalize("what is ohms law", buf + 80, sizeof(buf) - 80);
    checkStr(buf, buf + 80, "ohm's law and ohms law normalize identically");

    AmelTechText::normalize("HOW MANY SEC R THERE IN 1 MIN", buf, sizeof(buf));
    checkStr(buf, "how many seconds are there in one minute",
             "shorthand and digits expand");

    AmelTechText::normalize("", buf, sizeof(buf));
    checkStr(buf, "", "empty input gives empty output");

    AmelTechText::normalize("!!! ??? ...", buf, sizeof(buf));
    checkStr(buf, "", "punctuation only gives empty output");

    // Overlong input must truncate, never overflow.
    std::string huge(500, 'a');
    char small[24];
    AmelTechText::normalize(huge.c_str(), small, sizeof(small));
    check(strlen(small) < sizeof(small), "long input truncates within the buffer");

    check(AmelTechText::editSimilarity("gravity", "gravity") > 0.999f,
          "editSimilarity is 1.0 for identical strings");
    check(AmelTechText::editSimilarity("gravity", "gravty") > 0.75f,
          "editSimilarity tolerates a single typo");
    check(AmelTechText::editSimilarity("gravity", "banana") < 0.4f,
          "editSimilarity rejects unrelated words");
}

// ---------------------------------------------------------------------------
static void testMatcher(AmelTechBot& bot) {
    section("matcher");

    struct Probe {
        const char* question;
        float minConfidence;
        const char* mustContain;
    };

    static const Probe probes[] = {
        {"what is water",                 0.95f, "hydrogen"},
        {"What is WiFi?",                 0.90f, "wireless"},
        {"what is wi-fi",                 0.90f, "wireless"},
        {"whats esp32",                   0.85f, nullptr},
        {"what is i2c",                   0.90f, nullptr},
        {"what is teh speed of light",    0.70f, "299"},
        {"waht is gravity",               0.60f, nullptr},
        {"tell me about bluetooth",       0.74f, "Bluetooth"},
        {"what is ohms law",              0.74f, nullptr},
        {"explain i2c",                   0.74f, nullptr},
    };

    for (const Probe& p : probes) {
        String answer = bot.ask(p.question);
        float conf = bot.getConfidence();

        char label[128];
        snprintf(label, sizeof(label), "\"%s\" scores >= %.2f (got %.3f)",
                 p.question, (double)p.minConfidence, (double)conf);
        check(conf >= p.minConfidence, label);

        if (p.mustContain) {
            snprintf(label, sizeof(label), "\"%s\" answer mentions \"%s\"",
                     p.question, p.mustContain);
            check(contains(answer, p.mustContain), label);
        }
    }

    // The bot must admit ignorance rather than invent something.
    String unknown = bot.ask("what is the flibbertigibbet protocol");
    check(bot.getConfidence() < AMELTECH_CONF_WEAK,
          "nonsense question scores below the weak threshold");
    check(contains(unknown, "not") || contains(unknown, "teach"),
          "nonsense question produces an honest non-answer");

    // Speed: the two stage scan exists to keep this in microseconds.
    bot.ask("what is a capacitor");
    uint32_t us = bot.getLastScanMicros();
    char label[96];
    snprintf(label, sizeof(label), "scan completes quickly (%u us)", (unsigned)us);
    check(us < 200000u, label);

    check(bot.knowledge().lastCandidateCount() <= AMELTECH_CANDIDATE_POOL,
          "only the candidate pool is fully scored");

    // Repeating a question must hit the cache.
    uint32_t before = bot.knowledge().cacheHits();
    bot.ask("what is a capacitor");
    check(bot.knowledge().cacheHits() > before, "repeated question hits the cache");
}

// ---------------------------------------------------------------------------
static void testCalculator(AmelTechBot& bot) {
    section("calculator");

    struct Case { const char* expr; double want; };
    static const Case cases[] = {
        {"2 + 2",                4.0},
        {"25 * 4",               100.0},
        {"(12 + 8) / 5",         4.0},
        {"2^10",                 1024.0},
        {"-2^2",                -4.0},          // power binds tighter than unary minus
        {"2^3^2",                512.0},        // right associative
        {"7!",                   5040.0},
        {"sqrt(144)",            12.0},
        {"cbrt(27)",             3.0},
        {"3(4 + 5)",             27.0},         // implicit multiplication
        {"2pi",                  6.283185307},
        {"|-5|",                 5.0},
        {"12 x 4",               48.0},
        {"what is 7 squared",    49.0},
        {"15% of 200",           30.0},
        {"200 + 10%",            220.0},
        {"50%",                  0.5},
        {"10 % 3",               1.0},           // modulo
        {"log10(1000)",          3.0},
        {"log2(256)",            8.0},
        {"hypot(3, 4)",          5.0},
        {"gcd(48, 18)",          6.0},
        {"lcm(4, 6)",            12.0},
        {"min(3, 9)",            3.0},
        {"max(3, 9)",            9.0},
        {"1.5e3 + 500",          2000.0},
        {"round(2.5)",           3.0},
        {"floor(-2.5)",         -3.0},
        {"abs(-7) + sign(-3)",   6.0},
    };

    Calculator& calc = bot.calculator();
    calc.setAngleMode(CALC_RADIANS);

    for (const Case& c : cases) {
        double got = 0.0;
        bool ok = calc.evaluateTo(c.expr, got);
        char label[96];
        snprintf(label, sizeof(label), "%s", c.expr);
        if (!ok) {
            check(false, label);
            continue;
        }
        checkNear(got, c.want, 1e-6, label);
    }

    double v = 0.0;
    calc.setAngleMode(CALC_DEGREES);
    check(calc.evaluateTo("sin(30)", v), "sin(30) evaluates in degree mode");
    checkNear(v, 0.5, 1e-9, "sin(30) in degrees is 0.5");
    calc.setAngleMode(CALC_RADIANS);

    check(calc.evaluateTo("sin(pi / 2)", v), "sin(pi/2) evaluates in radian mode");
    checkNear(v, 1.0, 1e-9, "sin(pi/2) in radians is 1.0");

    // Errors must be reported, not papered over.
    check(!calc.evaluateTo("10 / 0", v), "division by zero is refused");
    check(calc.lastError() == CALC_DIV_ZERO, "division by zero reports CALC_DIV_ZERO");

    check(!calc.evaluateTo("2 + +", v), "malformed expression is refused");
    check(!calc.evaluateTo("(1 + 2", v), "unbalanced parenthesis is refused");
    check(calc.lastError() == CALC_PAREN, "unbalanced parenthesis reports CALC_PAREN");
    check(!calc.evaluateTo("sqrt(-1)", v), "sqrt of a negative number is refused");
    check(calc.lastError() == CALC_DOMAIN, "sqrt(-1) reports CALC_DOMAIN");
    check(!calc.evaluateTo("wibble(2)", v), "unknown function is refused");

    // A v1 defect: expressions over 95 characters were silently truncated and
    // the fragment was evaluated. Now they are refused outright.
    std::string longExpr = "1";
    for (int i = 0; i < 120; ++i) longExpr += "+1";
    check(!calc.evaluateTo(longExpr.c_str(), v),
          "over-long expression is refused, not truncated");

    // Plain questions must never be mistaken for maths. This was the defect
    // that made "what is wifi" answer "unknown function".
    char prepared[AMELTECH_CALC_MAX_EXPR];
    check(!Calculator::extractExpression("what is wifi", prepared, sizeof(prepared)) ||
          !Calculator::isCalculableExpression(prepared),
          "\"what is wifi\" is not treated as maths");
    check(!Calculator::extractExpression("tell me about bluetooth", prepared,
                                         sizeof(prepared)) ||
          !Calculator::isCalculableExpression(prepared),
          "\"tell me about bluetooth\" is not treated as maths");
    check(Calculator::extractExpression("what is 25*4", prepared, sizeof(prepared)),
          "\"what is 25*4\" is recognised as maths");

    // And through the full pipeline.
    String reply = bot.ask("what is 25 * 4");
    check(contains(reply, "100"), "bot answers 25 * 4 with 100");
}

// ---------------------------------------------------------------------------
static void testNameExtraction() {
    section("name and field extraction");

    char name[AMELTECH_PROFILE_NAME_LEN];
    char field[AMELTECH_PROFILE_FIELD_LEN];

    struct NameCase { const char* text; const char* want; };
    static const NameCase good[] = {
        {"hi my name is joky pk",                    "Joky Pk"},
        {"my name is arjun",                         "Arjun"},
        {"My name is Arjun. I'm an engineering student", "Arjun"},
        {"i am called Ravi",                         "Ravi"},
        {"you can call me Sam",                      "Sam"},
        {"hello, this is Priya",                     "Priya"},
        {"im Deepak",                                "Deepak"},
    };

    for (const NameCase& c : good) {
        bool ok = UserProfileStore::extractName(c.text, name, sizeof(name));
        char label[128];
        snprintf(label, sizeof(label), "extract \"%s\" -> %s", c.text, c.want);
        if (!ok) { check(false, label); continue; }
        checkStr(name, c.want, label);
    }

    // Things that look like introductions but are not.
    static const char* const bad[] = {
        "i am fine",
        "i am tired",
        "i am going home",
        "i am a student",          // a field, not a name
        "i am an engineer",
        "what is your name",
        "hello there",
        nullptr
    };
    for (int i = 0; bad[i]; ++i) {
        char label[128];
        snprintf(label, sizeof(label), "\"%s\" is not read as a name", bad[i]);
        check(!UserProfileStore::extractName(bad[i], name, sizeof(name)), label);
    }

    check(UserProfileStore::extractField("My name is Arjun. I'm an engineering student",
                                         field, sizeof(field)),
          "field extracted from a two sentence introduction");
    check(strstr(field, "engineering") != nullptr, "field contains \"engineering\"");

    check(UserProfileStore::extractField("i am a doctor", field, sizeof(field)),
          "field extracted from \"i am a doctor\"");
    checkStr(field, "doctor", "field is \"doctor\"");

    check(!UserProfileStore::extractField("i am here", field, sizeof(field)),
          "\"i am here\" is not read as a profession");

    check(UserProfileStore::isPlausibleName("Joky Pk"), "\"Joky Pk\" is a plausible name");
    check(!UserProfileStore::isPlausibleName("fine"), "\"fine\" is not a plausible name");
    check(!UserProfileStore::isPlausibleName("a"), "a single letter is not a name");
    check(!UserProfileStore::isPlausibleName("one two three four"),
          "four words is not a name");
    check(!UserProfileStore::isPlausibleName("Ravi123"), "digits are not allowed in a name");
}

// ---------------------------------------------------------------------------
static void testProfileStore() {
    section("profile store");

    UserProfileStore store;
    store.begin();
    store.clear();

    check(store.capacity() == 34, "capacity is 34 names");
    check(store.count() == 0, "store starts empty");

    check(store.addOrTouch("Joky Pk", nullptr) >= 0, "first name is stored");
    check(store.addOrTouch("Arjun", "engineering student") >= 0,
          "second name is stored with a field");
    check(store.count() == 2, "count is 2");

    check(store.findByName("joky pk") >= 0, "lookup is case insensitive");
    check(store.findByName("Nobody") < 0, "unknown name is not found");

    // Most recently seen first.
    const UserProfileEntry* newest = store.byRecency(0);
    check(newest && strcmp(newest->name, "Arjun") == 0, "most recent name ranks first");

    store.touch(store.findByName("Joky Pk"));
    newest = store.byRecency(0);
    check(newest && strcmp(newest->name, "Joky Pk") == 0,
          "touching a name moves it to the front");

    // Fill past capacity and confirm the oldest is evicted.
    store.clear();
    char nameBuf[32];
    for (int i = 0; i < 36; ++i) {
        snprintf(nameBuf, sizeof(nameBuf), "Person%c%c",
                 (char)('a' + i / 26), (char)('a' + i % 26));
        store.addOrTouch(nameBuf, nullptr);
    }
    check(store.count() == 34, "store holds at most 34 names");
    check(store.findByName("Personaa") < 0, "oldest name was evicted");
    check(store.findByName("Personab") < 0, "second oldest name was evicted");
    check(store.findByName("Personbj") >= 0, "newest name is present");

    // Persistence.
    store.clear();
    store.addOrTouch("Joky Pk", nullptr);
    store.addOrTouch("Arjun", "engineering student");
    check(store.save() == 0, "store saves to NVS");

    UserProfileStore reloaded;
    reloaded.begin();
    check(reloaded.count() == 2, "two names survive a reload");
    check(reloaded.findByName("Arjun") >= 0, "Arjun survives a reload");
    const UserProfileEntry* e = reloaded.at(reloaded.findByName("Arjun"));
    check(e && strstr(e->field, "engineering") != nullptr, "field survives a reload");

    const UserProfileEntry* first = reloaded.byRecency(0);
    check(first && strcmp(first->name, "Arjun") == 0,
          "recency order survives a reload");

    check(reloaded.removeByName("Arjun"), "a name can be removed");
    check(reloaded.count() == 1, "count drops after removal");
    reloaded.clear();
    reloaded.save();
}

// ---------------------------------------------------------------------------
static void testIdentityFlow() {
    section("identity conversation");

    // Seed three names, then simulate a power cycle.
    {
        AmelTechBot bot;
        bot.begin();
        bot.forgetAllUsers();
        bot.ask("hi my name is joky pk");
        bot.ask("my name is joky py");
        bot.ask("My name is Arjun. I'm an engineering student");
        check(bot.getUserProfileCount() == 3, "three names were captured from chat");
        bot.end();
    }

    {
        AmelTechBot bot;
        bot.begin();
        check(bot.getUserProfileCount() == 3, "names survive the power cycle");

        // The first message of the session triggers the confirmation.
        String first = bot.ask("what is the speed of light");
        check(contains(first, "Are you Arjun?"),
              "first message after reset asks about the most recent name");

        String second = bot.ask("no");
        check(contains(second, "Are you Joky Py?"), "\"no\" offers the next name");

        String third = bot.ask("no");
        check(contains(third, "Are you Joky Pk?"), "\"no\" offers the third name");

        String fourth = bot.ask("yes");
        check(contains(fourth, "Joky Pk"), "\"yes\" confirms the identity");
        check(contains(fourth, "299"),
              "the interrupted question is answered after confirmation");
        checkStr(bot.getUserName(), "Joky Pk", "active user is set");

        // The name is used again after the mention gap, not on every reply.
        bot.ask("what is gravity");
        bot.ask("what is water");
        String gapped = bot.ask("what is a resistor");
        check(contains(gapped, "Joky Pk") ||
              contains(bot.ask("what is a diode"), "Joky Pk"),
              "the name reappears after the mention gap");

        bot.end();
    }

    // Four rejections must end the guessing.
    {
        AmelTechBot bot;
        bot.begin();
        bot.forgetAllUsers();
        for (int i = 0; i < 6; ++i) {
            char intro[48];
            snprintf(intro, sizeof(intro), "my name is Guest%c", (char)('a' + i));
            bot.ask(intro);
        }
        bot.end();

        AmelTechBot fresh;
        fresh.begin();
        String r = fresh.ask("what is ohms law");
        check(contains(r, "Are you"), "confirmation starts after reset");

        String r1 = fresh.ask("no");
        String r2 = fresh.ask("no");
        String r3 = fresh.ask("no");
        check(contains(r1, "Are you") && contains(r2, "Are you") && contains(r3, "Are you"),
              "three rejections keep offering names");

        String r4 = fresh.ask("no");
        checkStr(r4.c_str(), "How are you,.. What is your name?",
                 "the fourth rejection asks the open question");

        String named = fresh.ask("My name is Ravi. I am a doctor");
        check(contains(named, "Ravi"), "the new name is acknowledged");
        check(contains(named, "doctor"), "the field is acknowledged");
        check(contains(named, "Ohm") || contains(named, "current"),
              "the deferred question is answered afterwards");

        checkStr(fresh.getUserName(), "Ravi", "the new name becomes the active user");
        check(fresh.getUserField() && strstr(fresh.getUserField(), "doctor"),
              "the new field is stored");

        String who = fresh.ask("what is my name");
        check(contains(who, "Ravi"), "the bot can recall the name on request");

        fresh.forgetAllUsers();
        fresh.end();
    }
}

// ---------------------------------------------------------------------------
static void testTrainingConsole() {
    section("training console");

    AmelTechBot bot;
    bot.begin();
    bot.clearKnowledge();
    bot.saveKnowledge();

    check(TrainingConsole::isTrainingCommand("train | a | b"),
          "\"train | ...\" is a training command");
    check(TrainingConsole::isTrainingCommand("TRAIN|a|b"),
          "the keyword is case insensitive and needs no space");
    check(!TrainingConsole::isTrainingCommand("training courses near me"),
          "\"training courses\" is ordinary chat");
    check(!TrainingConsole::isTrainingCommand("what is a train"),
          "\"what is a train\" is ordinary chat");

    String r = bot.handleSerialLine("train | who made you | AmelTech labs made me.");
    checkStr(r.c_str(), "train successfully and save data number code 0001",
             "the success wording and the first data code are exact");

    r = bot.handleSerialLine("train | what is our motto | Measure it, or do not claim it.");
    checkStr(r.c_str(), "train successfully and save data number code 0002",
             "the second lesson gets code 0002");

    String answer = bot.ask("who made you");
    check(contains(answer, "AmelTech labs"), "a taught answer is returned");

    // Taught entries beat built-in ones for the same question.
    bot.handleSerialLine("train | what is wifi | For this class, our lab access point.");
    check(contains(bot.ask("what is wifi"), "lab access point"),
          "a taught entry overrides the built-in answer");

    r = bot.handleSerialLine("train | list");
    check(contains(r, "0001") && contains(r, "who made you"),
          "list shows codes and questions");

    r = bot.handleSerialLine("train | delete | 0001");
    check(contains(r, "0001") && contains(r, "delete"),
          "deleting by code confirms the code");
    check(bot.getUserCount() == 2, "one entry was removed");

    r = bot.handleSerialLine("train | delete | 9999");
    check(contains(r, "No taught entry"), "deleting an unknown code is reported");

    r = bot.handleSerialLine("train | delete | full data");
    check(contains(r, "delete"), "full data delete is confirmed");
    check(bot.getUserCount() == 0, "all taught entries were removed");
    check(bot.getBuiltinCount() > 2000, "built-in knowledge is untouched");

    r = bot.handleSerialLine("train | delete | full data");
    check(contains(r, "nothing"), "deleting an empty set says so");

    // Malformed commands must explain themselves.
    r = bot.handleSerialLine("train | only a question");
    check(contains(r, "answer"), "a missing answer is explained");

    r = bot.handleSerialLine("train | help");
    check(contains(r, "train | delete | full data"), "help lists the delete command");

    r = bot.handleSerialLine("train | status");
    check(contains(r, "Reserved heap"), "status reports the reserved heap");

    // The heap guard: set the reserve above all available memory and confirm
    // that training is refused with an explanation rather than failing quietly.
    bot.training().setMinFreeHeap(0xFFFFFFFFUL);
    r = bot.handleSerialLine("train | blocked question | blocked answer");
    check(contains(r, "reserved") || contains(r, "paused"),
          "training is refused when the heap reserve is not met");
    check(bot.getUserCount() == 0, "nothing was stored while blocked");
    bot.training().setMinFreeHeap(AMELTECH_TRAIN_MIN_FREE_HEAP);

    r = bot.handleSerialLine("train | after unblock | it works again");
    check(contains(r, "train successfully"), "training resumes once heap is available");

    bot.clearKnowledge();
    bot.saveKnowledge();
    bot.end();
}

// ---------------------------------------------------------------------------
static void testKnowledgePersistence() {
    section("knowledge persistence");

    {
        AmelTechBot bot;
        bot.begin();
        bot.clearKnowledge();
        bot.train("what is the lab name", "AmelTech labs.");
        bot.train("who is the teacher", "Mr Amel.");
        check(bot.getUserCount() == 2, "two entries taught");
        check(bot.saveKnowledge() == AMELTECH_OK, "knowledge saves");
        bot.end();
    }

    {
        AmelTechBot bot;
        bot.begin();
        check(bot.getUserCount() == 2, "taught entries survive a power cycle");
        check(contains(bot.ask("what is the lab name"), "AmelTech labs"),
              "a reloaded answer is still returned");

        // Codes must not be reused after a reload.
        uint16_t next = bot.knowledge().peekNextCode();
        check(next >= 3, "the next data code continues after a reload");

        bot.clearKnowledge();
        bot.saveKnowledge();
        bot.end();
    }

    {
        AmelTechBot bot;
        bot.begin();
        check(bot.getUserCount() == 0, "cleared knowledge stays cleared");
        bot.end();
    }
}

// ---------------------------------------------------------------------------
static void testSensorMath() {
    section("DHT maths");

    // Reference values for 25 C at 60% RH.
    checkNear(SensorHub::dewPoint(25.0f, 60.0f), 16.7, 0.4, "dew point at 25 C / 60% RH");
    checkNear(SensorHub::dewPoint(20.0f, 50.0f), 9.3, 0.4, "dew point at 20 C / 50% RH");
    checkNear(SensorHub::absoluteHumidity(25.0f, 60.0f), 13.8, 0.6,
              "absolute humidity at 25 C / 60% RH");

    // Below the Rothfusz threshold the heat index equals the temperature.
    checkNear(SensorHub::heatIndex(20.0f, 50.0f), 20.0, 0.6,
              "heat index equals temperature in mild conditions");
    check(SensorHub::heatIndex(35.0f, 70.0f) > 35.0f,
          "heat index exceeds temperature when hot and humid");

    check(SensorHub::classifyComfort(22.0f, 45.0f) == COMFORT_IDEAL,
          "22 C at 45% RH is ideal");
    check(SensorHub::classifyComfort(5.0f, 45.0f) == COMFORT_COLD, "5 C is cold");
    check(SensorHub::classifyComfort(40.0f, 80.0f) == COMFORT_DANGEROUS,
          "40 C at 80% RH is dangerous");

    // With no sensor attached the bot must refuse to invent a reading.
    AmelTechBot bot;
    bot.begin();
    check(!bot.hasSensor(), "no sensor is configured by default");

    String reply = bot.ask("what is the temperature");
    check(contains(reply, "no temperature") || contains(reply, "not invent"),
          "the bot admits it has no sensor");
    check(!contains(reply, " C."), "no fabricated temperature is reported");

    String situation = bot.getSituationReport();
    check(contains(situation, "without a sensor") || contains(situation, "Connect"),
          "the situation report asks for a sensor rather than guessing");

    const DhtReading& r = bot.getSensorReading();
    check(r.status != MEAS_LIVE, "an unconfigured sensor never reports LIVE");

    bot.end();
}

// ---------------------------------------------------------------------------
static void testDiagnostics() {
    section("diagnostics and health");

    AmelTechBot bot;
    bot.begin();

    const HealthReport& hr = bot.diagnostics().evaluateHealth();
    check(hr.componentCount == AMELTECH_HEALTH_COMPONENTS,
          "every component is present in the report");

    uint32_t totalWeight = 0;
    for (uint8_t i = 0; i < hr.componentCount; ++i) {
        totalWeight += hr.components[i].weight;
        check(hr.components[i].score <= 100, "component score stays within 0-100");
        check(hr.components[i].confidence <= 100, "component confidence stays within 0-100");
        check(hr.components[i].name != nullptr, "component has a name");
    }
    check(totalWeight == 100, "component weights sum to 100");

    check(hr.overallScore >= 0 && hr.overallScore <= 100,
          "overall score stays within 0-100");
    check(hr.overallConfidence <= 100, "overall confidence stays within 0-100");

    // The old scoring averaged a flat 50 for anything unmeasured, so a board
    // with no sensors looked half broken. Unmeasured components are now simply
    // excluded and the report says how much it actually measured.
    for (uint8_t i = 0; i < hr.componentCount; ++i) {
        if (hr.components[i].confidence == 0) {
            check(hr.components[i].level == HEALTH_UNKNOWN,
                  "an unmeasured component is reported as UNKNOWN");
            check(hr.components[i].detail[0] != '\0',
                  "an unmeasured component explains why");
        }
    }

    String text = bot.getHealthReport();
    check(contains(text, "ESP32 Health"), "health report has a heading");
    check(contains(text, "confidence"), "health report states its confidence");

    String full = bot.runDiagnostics(true);
    check(contains(full, "AmelTech Diagnostics"), "full diagnostics has a heading");
    check(contains(full, AMELTECH_VERSION_STRING), "full diagnostics reports the version");

    // Telemetry honesty: nothing may be reported LIVE on a host build.
    const ESP32Telemetry& t = bot.getTelemetry(true);
    check(t.temperatureC.status != MEAS_LIVE,
          "no live die temperature is claimed on this build");
    check(!Telemetry::statusIsUsable(t.freeHeap.status) ||
          t.freeHeap.value > 0,
          "a usable heap reading is non-zero");

    check(strcmp(Telemetry::statusToString(MEAS_UNSUPPORTED), "UNSUPPORTED") == 0,
          "status strings are stable");
    check(!Telemetry::statusIsUsable(MEAS_ERROR), "an error status is not usable");
    check(Telemetry::statusIsUsable(MEAS_LIVE), "a live status is usable");

    bot.end();
}

// ---------------------------------------------------------------------------
static void testConversationSafety() {
    section("conversation robustness");

    AmelTechBot bot;
    bot.begin();
    bot.forgetAllUsers();
    bot.clearKnowledge();

    check(bot.ask("").length() > 0, "an empty question still gets a reply");
    check(bot.getLastError() == AMELTECH_INVALID_INPUT,
          "an empty question sets INVALID_INPUT");

    std::string huge(400, 'q');
    String reply = bot.ask(huge.c_str());
    check(reply.length() > 0, "an over-long question still gets a reply");
    check(bot.getLastError() == AMELTECH_OVERFLOW,
          "an over-long question sets OVERFLOW");

    check(bot.ask("   ").length() > 0, "whitespace only input is handled");
    check(bot.ask("!@#$%^&*()").length() > 0, "symbol soup is handled");

    // Small talk should not be swallowed by the knowledge base.
    check(contains(bot.ask("hello"), "Hello"), "greeting is recognised");
    check(contains(bot.ask("thanks"), "welcome"), "thanks is recognised");
    check(contains(bot.ask("who are you"), "AmelTech"), "self query is recognised");

    // Context and follow-up.
    bot.resetContext();
    check(bot.getContextSize() == AMELTECH_MAX_CONTEXT, "context size defaults to the maximum");
    bot.ask("what is a capacitor");
    String more = bot.ask("tell me more");
    check(more.length() > 0, "a follow-up gets a reply");

    bot.setContextSize(0);
    check(bot.getContextSize() == 0, "context can be disabled");
    bot.setContextSize(AMELTECH_MAX_CONTEXT);

    // Several thousand queries must not leak or crash.
    for (int i = 0; i < 300; ++i) {
        char q[48];
        snprintf(q, sizeof(q), "what is test topic %d", i);
        bot.ask(q);
    }
    check(true, "300 mixed queries complete without a crash");

    check(strcmp(AmelTechBot::errorToString(AMELTECH_OK), "OK") == 0,
          "error strings are stable");
    check(strcmp(AmelTechBot::version(), AMELTECH_VERSION_STRING) == 0,
          "version string matches the configured value");

    bot.end();
}

// ---------------------------------------------------------------------------
int main() {
    printf("AmelTech lab's bot - host test suite\n");
    printf("version %s\n", AMELTECH_VERSION_STRING);

    // Start from a clean simulated flash so results are reproducible.
    Preferences::hostEraseAll();

    testNormalizer();
    testNameExtraction();
    testProfileStore();

    {
        AmelTechBot bot;
        bot.begin();
        bot.clearKnowledge();
        bot.forgetAllUsers();

        printf("\n  knowledge entries: %u built in, %u taught\n",
               (unsigned)bot.getBuiltinCount(), (unsigned)bot.getUserCount());

        testMatcher(bot);
        testCalculator(bot);
        bot.end();
    }

    Preferences::hostEraseAll();
    testIdentityFlow();

    Preferences::hostEraseAll();
    testTrainingConsole();

    Preferences::hostEraseAll();
    testKnowledgePersistence();

    Preferences::hostEraseAll();
    testSensorMath();
    testDiagnostics();
    testConversationSafety();

    printf("\n===========================================\n");
    printf("  checks run : %d\n", g_checks);
    printf("  failures   : %d\n", g_failures);
    printf("  result     : %s\n", g_failures == 0 ? "PASS" : "FAIL");
    printf("===========================================\n");

    return g_failures == 0 ? 0 : 1;
}
