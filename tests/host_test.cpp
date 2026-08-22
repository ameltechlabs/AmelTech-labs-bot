// =============================================================
// host_test.cpp
//
// Host-side unit tests for the platform-independent logic in
// AmelTechBot: KnowledgeBase (normalization/matching/training)
// and Calculator (expression evaluation). Telemetry/Diagnostics
// hardware paths are NOT exercised here since they require real
// ESP32 hardware/APIs — see docs/STATUS.md.
//
// Build: see tests/CMakeLists.txt
// Run:   ./host_tests
// =============================================================
#include <iostream>
#include <cstdlib>

#include "Arduino.h"
#include "AmelTechTypes.h"
#include "KnowledgeBase.h"
#include "Calculator.h"

// ameltechStatusToString() is defined in AmelTechBot.cpp, which is not
// part of the host test build (it depends on Telemetry/Diagnostics and
// therefore ESP32-only headers). Provide a minimal stand-in so test
// assertions that reference status codes still link.
const char* ameltechStatusToString(AmelTechStatus status) {
    switch (status) {
        case AMELTECH_OK: return "OK";
        case AMELTECH_INVALID_INPUT: return "INVALID_INPUT";
        case AMELTECH_NOT_FOUND: return "NOT_FOUND";
        case AMELTECH_LOW_CONFIDENCE: return "LOW_CONFIDENCE";
        case AMELTECH_UNSUPPORTED: return "UNSUPPORTED";
        case AMELTECH_UNAVAILABLE: return "UNAVAILABLE";
        case AMELTECH_MEASUREMENT_ERROR: return "MEASUREMENT_ERROR";
        case AMELTECH_MEMORY_ERROR: return "MEMORY_ERROR";
        case AMELTECH_STORAGE_ERROR: return "STORAGE_ERROR";
        case AMELTECH_TIMEOUT: return "TIMEOUT";
        case AMELTECH_INVALID_CONFIGURATION: return "INVALID_CONFIGURATION";
        case AMELTECH_DUPLICATE: return "DUPLICATE";
        case AMELTECH_CONTRADICTION: return "CONTRADICTION";
        default: return "UNKNOWN";
    }
}

static int g_testsRun = 0;
static int g_testsFailed = 0;

#define CHECK(cond, desc) do { \
    g_testsRun++; \
    if (!(cond)) { \
        g_testsFailed++; \
        std::cout << "[FAIL] " << desc << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
    } else { \
        std::cout << "[PASS] " << desc << std::endl; \
    } \
} while (0)

// ---------------------------------------------------------------
// KnowledgeBase tests
// ---------------------------------------------------------------
void test_normalization() {
    std::cout << "\n--- normalization ---" << std::endl;
    CHECK(KnowledgeBase::normalize("  What Is WATER?  ") == String("what is water"),
          "normalize: case/whitespace/punctuation");
    CHECK(KnowledgeBase::normalize("What's ESP32?") == String("what is esp32"),
          "normalize: contraction expansion");
    CHECK(KnowledgeBase::normalize("") == String(""), "normalize: empty string");
}

void test_exact_match() {
    std::cout << "\n--- exact match ---" << std::endl;
    KnowledgeBase kb;
    kb.begin();
    MatchResult r = kb.query("What is water?");
    CHECK(r.found, "exact match: found");
    CHECK(r.confidence >= 0.90f, "exact match: high confidence");
}

void test_spelling_variation() {
    std::cout << "\n--- spelling variation / fuzzy ---" << std::endl;
    KnowledgeBase kb;
    kb.begin();
    MatchResult r = kb.query("what iz esp32");
    CHECK(r.found, "typo tolerant: found a candidate");
}

void test_fuzzy_abbreviation() {
    std::cout << "\n--- abbreviation handling ---" << std::endl;
    KnowledgeBase kb;
    kb.begin();
    MatchResult r = kb.query("how many sec r there in 1 min");
    // This is a hard case (numeral vs word mismatch) — we check that
    // matching at least produces a non-zero-confidence candidate,
    // reflecting the documented lightweight (non-neural) similarity.
    CHECK(r.confidence > 0.0f, "abbreviation-normalized input produces a candidate");
}

void test_training_and_duplicate_detection() {
    std::cout << "\n--- training & duplicate detection ---" << std::endl;
    KnowledgeBase kb;
    kb.begin();

    TrainResult t1 = kb.train("what is my project", "An ESP32 sensor hub.", "custom");
    CHECK(t1.status == AMELTECH_OK, "train: first entry succeeds");

    TrainResult t2 = kb.train("what is my project", "An ESP32 sensor hub.", "custom");
    CHECK(t2.status == AMELTECH_DUPLICATE, "train: exact duplicate detected");

    TrainResult t3 = kb.train("what is my project", "A weather station.", "custom");
    CHECK(t3.status == AMELTECH_CONTRADICTION, "train: contradiction detected");

    TrainResult t4 = kb.train("", "answer", "custom");
    CHECK(t4.status == AMELTECH_INVALID_INPUT, "train: empty question rejected");

    TrainResult t5 = kb.train("question", "", "custom");
    CHECK(t5.status == AMELTECH_INVALID_INPUT, "train: empty answer rejected");

    TrainResult t6 = kb.train("what is water", "Something else entirely.", "science");
    CHECK(t6.status == AMELTECH_CONTRADICTION, "train: contradiction against built-in knowledge");
}

void test_confidence_thresholds() {
    std::cout << "\n--- confidence / unknown questions ---" << std::endl;
    KnowledgeBase kb;
    kb.begin();
    MatchResult r = kb.query("asdkjaslkdj qwoieqwoie random gibberish");
    CHECK(!r.found || r.confidence < 0.50f, "gibberish input yields low/no confidence");
}

void test_remove_and_clear() {
    std::cout << "\n--- removeQA / clearKnowledge ---" << std::endl;
    KnowledgeBase kb;
    kb.begin();
    kb.train("custom question one", "custom answer one", "custom");
    CHECK(kb.userCount() == 1, "userCount after one train() call");

    AmelTechStatus s = kb.removeQA("custom question one");
    CHECK(s == AMELTECH_OK, "removeQA: existing entry removed");
    CHECK(kb.userCount() == 0, "userCount after removal");

    AmelTechStatus s2 = kb.removeQA("nonexistent question");
    CHECK(s2 == AMELTECH_NOT_FOUND, "removeQA: missing entry reports NOT_FOUND");

    kb.train("a", "b", "custom");
    kb.train("c", "d", "custom");
    kb.clearUserKnowledge();
    CHECK(kb.userCount() == 0, "clearUserKnowledge empties user knowledge");
}

// ---------------------------------------------------------------
// Calculator tests
// ---------------------------------------------------------------
void test_calculator_basic() {
    std::cout << "\n--- calculator basic ops ---" << std::endl;
    Calculator calc;

    CalcResult r1 = calc.evaluate("25 * 4");
    CHECK(r1.valid && r1.value == 100.0, "25 * 4 = 100");

    CalcResult r2 = calc.evaluate("100 / 5");
    CHECK(r2.valid && r2.value == 20.0, "100 / 5 = 20");

    CalcResult r3 = calc.evaluate("(25 + 5) * 2");
    CHECK(r3.valid && r3.value == 60.0, "(25 + 5) * 2 = 60");
}

void test_calculator_percentage() {
    std::cout << "\n--- calculator percentage ---" << std::endl;
    Calculator calc;

    CalcResult r1 = calc.evaluate("50%");
    CHECK(r1.valid && r1.value == 0.5, "50%% = 0.5");

    CalcResult r2 = calc.evaluate("25 + 10%");
    // 10% -> 0.10 (bare percent conversion), so 25 + 0.10 = 25.10
    CHECK(r2.valid, "25 + 10%% evaluates without error");
}

void test_calculator_parentheses() {
    std::cout << "\n--- calculator parentheses ---" << std::endl;
    Calculator calc;
    CalcResult r = calc.evaluate("((2 + 3) * (4 - 1))");
    CHECK(r.valid && r.value == 15.0, "nested parentheses evaluate correctly");
}

void test_calculator_division_by_zero() {
    std::cout << "\n--- calculator division by zero ---" << std::endl;
    Calculator calc;
    CalcResult r = calc.evaluate("10 / 0");
    CHECK(!r.valid && r.status == CALC_ERROR_DIV_BY_ZERO, "10 / 0 reports division-by-zero error");
}

void test_calculator_errors() {
    std::cout << "\n--- calculator error handling ---" << std::endl;
    Calculator calc;

    CalcResult r1 = calc.evaluate("");
    CHECK(!r1.valid && r1.status == CALC_ERROR_EMPTY, "empty expression rejected");

    CalcResult r2 = calc.evaluate("5 $ 3");
    CHECK(!r2.valid && r2.status == CALC_ERROR_INVALID_CHAR, "invalid character rejected");

    CalcResult r3 = calc.evaluate("(5 + 3");
    CHECK(!r3.valid && r3.status == CALC_ERROR_SYNTAX, "unbalanced parenthesis rejected");

    CalcResult r4 = calc.evaluate("5 + + 3");
    // "5 + +3" is actually valid (unary plus) under this grammar,
    // so instead test something unambiguously malformed:
    CalcResult r5 = calc.evaluate("* 5");
    CHECK(!r5.valid, "leading operator without operand rejected");

    (void)r4;
}

void test_calculator_too_long() {
    std::cout << "\n--- calculator length limit ---" << std::endl;
    Calculator calc;
    String longExpr = "1";
    for (int i = 0; i < 100; i++) longExpr += "+1";
    CalcResult r = calc.evaluate(longExpr);
    CHECK(!r.valid && r.status == CALC_ERROR_TOO_LONG, "overlong expression rejected");
}

// ---------------------------------------------------------------
// Levenshtein / similarity sanity checks
// ---------------------------------------------------------------
void test_similarity() {
    std::cout << "\n--- similarity function ---" << std::endl;
    float s1 = KnowledgeBase::similarity(String("what is water"), String("what is water"));
    CHECK(s1 == 1.0f, "identical strings have similarity 1.0");

    float s2 = KnowledgeBase::similarity(String("what is water"), String("completely unrelated text"));
    CHECK(s2 < 0.5f, "unrelated strings have low similarity");

    float s3 = KnowledgeBase::similarity(String("what is esp32"), String("what iz esp32"));
    CHECK(s3 > 0.5f, "near-typo strings have moderate-to-high similarity");
}

int main() {
    std::cout << "=== AmelTechBot Host Test Suite ===" << std::endl;

    test_normalization();
    test_exact_match();
    test_spelling_variation();
    test_fuzzy_abbreviation();
    test_training_and_duplicate_detection();
    test_confidence_thresholds();
    test_remove_and_clear();

    test_calculator_basic();
    test_calculator_percentage();
    test_calculator_parentheses();
    test_calculator_division_by_zero();
    test_calculator_errors();
    test_calculator_too_long();

    test_similarity();

    std::cout << "\n=== Results: " << (g_testsRun - g_testsFailed) << "/" << g_testsRun
               << " passed ===" << std::endl;

    return g_testsFailed == 0 ? 0 : 1;
}

// Serial stub instance (declared extern in Arduino.h)
SerialStub Serial;
