/*
 * Host-side tests for AmelTech lab's bot (no ESP32 hardware required)
 * Build with CMake from tests/ directory.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

// Use stub Arduino before library headers
#include "host_stub/Arduino.h"

// Include library sources conceptually — for host we compile selected units
#include "../src/Calculator.h"
#include "../src/Calculator.cpp"
#include "../src/KnowledgeBase.h"
// knowledge_generated.h is included by KnowledgeBase.h
// We need KnowledgeBase.cpp logic — include carefully
#include "../src/KnowledgeBase.cpp"

static int g_failed = 0;
static int g_passed = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); ++g_failed; } \
    else { ++g_passed; } \
} while (0)

void test_calculator() {
    Calculator c;
    String r = c.evaluate("25 * 4");
    EXPECT(r == "100", "25*4");
    r = c.evaluate("100 / 5");
    EXPECT(r == "20", "100/5");
    r = c.evaluate("(25 + 5) * 2");
    EXPECT(r == "60", "(25+5)*2");
    r = c.evaluate("50%");
    EXPECT(r == "0.5", "50%");
    r = c.evaluate("10 / 0");
    EXPECT(r.length() == 0 && c.lastError() == CALC_DIV_ZERO, "div zero");
    r = c.evaluate("2 + +");
    EXPECT(c.lastError() != CALC_OK, "malformed");
    EXPECT(Calculator::looksLikeExpression("3+4"), "looks like expr");
    EXPECT(!Calculator::looksLikeExpression("what is water"), "not expr");
}

void test_normalize() {
    char out[128];
    KnowledgeBase::normalize("How many SEC r there in 1 MIN?", out, sizeof(out));
    EXPECT(strstr(out, "seconds") != nullptr, "sec->seconds");
    EXPECT(strstr(out, "minute") != nullptr, "min->minute");
}

void test_knowledge_match() {
    KnowledgeBase kb;
    kb.begin();
    MatchResult m = kb.findBest("what is water", 0.5f);
    EXPECT(m.found, "water found");
    EXPECT(m.confidence >= 0.9f, "water high conf");
    EXPECT(m.answer != nullptr && strstr(m.answer, "H2O") != nullptr, "water answer");

    m = kb.findBest("how many seconds are in one minute", 0.5f);
    EXPECT(m.found && m.confidence >= 0.9f, "seconds match");

    m = kb.findBest("how many sec r there in 1 min", 0.5f);
    EXPECT(m.found, "abbrev fuzzy match");
    EXPECT(m.confidence >= 0.55f, "abbrev confidence");

    m = kb.findBest("xyzzy completely unknown phrase 12345", 0.5f);
    EXPECT(!m.found || m.confidence < 0.5f, "unknown");
}

void test_training() {
    KnowledgeBase kb;
    kb.begin();
    int8_t rc = kb.addUser("what is my project", "Uses ESP32", "custom");
    EXPECT(rc == 0, "train ok");
    rc = kb.addUser("what is my project", "Uses ESP32", "custom");
    EXPECT(rc == -2, "duplicate");
    rc = kb.addUser("what is my project", "Totally different answer that conflicts", "custom");
    EXPECT(rc == -2 || rc == -3, "dup or conflict");
    MatchResult m = kb.findBest("what is my project", 0.5f);
    EXPECT(m.found && m.fromUser, "user match");
}

void test_fuzzy() {
    float s = KnowledgeBase::fuzzyScore("hello", "hello");
    EXPECT(s >= 0.99f, "exact fuzzy");
    s = KnowledgeBase::fuzzyScore("colour", "color");
    EXPECT(s > 0.5f, "near fuzzy");
    int d = KnowledgeBase::levenshtein("kitten", "sitting", 10);
    EXPECT(d == 3, "levenshtein kitten/sitting");
}

int main() {
    printf("AmelTech host tests\n");
    test_calculator();
    test_normalize();
    test_knowledge_match();
    test_training();
    test_fuzzy();
    printf("Passed: %d  Failed: %d\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}
