/*
 * Calculator.h
 * ---------------------------------------------------------------------------
 * Safe expression evaluator for embedded use.
 *
 * Supported
 *   + - * / % (modulo) ^ (power, right associative)
 *   unary + -, parentheses, |x| for absolute value, postfix ! for factorial
 *   postfix % for percent, with the shorthand people actually expect:
 *       50%            -> 0.5
 *       200 + 10%      -> 220        (10% *of 200*)
 *       200 - 10%      -> 180
 *       15% of 200     -> 30
 *   functions: sqrt cbrt abs sq sign exp ln log log2 log10 floor ceil round
 *              sin cos tan asin acos atan sinh cosh tanh deg rad fact
 *              pow min max mod hypot atan2 gcd lcm
 *   constants: pi e tau phi
 *   natural language: "what is 25 times 4", "square root of 144",
 *                     "7 squared", "15 percent of 200", "10 mod 3"
 *
 * Refused
 *   division and modulo by zero, malformed input, unknown identifiers,
 *   non-finite or overflowing results, expressions past the length, depth or
 *   operation budget. There is no eval, no variables and no code execution.
 *
 * Angle mode defaults to RADIANS, matching standard maths and the C library.
 * Call setAngleMode(CALC_DEGREES) if your users think in degrees.
 * ---------------------------------------------------------------------------
 */

#ifndef AMELTECH_CALCULATOR_H
#define AMELTECH_CALCULATOR_H

#include <Arduino.h>
#include "AmelTechConfig.h"

enum CalcError : int8_t {
    CALC_OK = 0,
    CALC_DIV_ZERO = -1,
    CALC_MALFORMED = -2,
    CALC_OVERFLOW = -3,
    CALC_INVALID_CHAR = -4,
    CALC_EMPTY = -5,
    CALC_PAREN = -6,
    CALC_UNKNOWN_FUNC = -7,
    CALC_DOMAIN = -8,
    CALC_TOO_LONG = -9,
    CALC_TOO_DEEP = -10,
    CALC_TOO_COMPLEX = -11,
    CALC_ARITY = -12
};

enum CalcAngleMode : uint8_t {
    CALC_RADIANS = 0,
    CALC_DEGREES = 1
};

class Calculator {
public:
    Calculator();

    // Evaluate. Returns the formatted result, or "" on failure with
    // lastError() set. Natural-language phrasing is accepted.
    String evaluate(const String& expression);
    String evaluate(const char* expression);

    // Evaluate into a double instead of a string.
    bool evaluateTo(const char* expression, double& out);

    CalcError lastError() const { return _err; }
    const char* lastErrorString() const;
    double lastValue() const { return _value; }
    // The symbolic form actually evaluated, after natural-language rewriting.
    const char* lastNormalizedExpression() const { return _buf; }
    uint16_t lastOpCount() const { return _ops; }

    void setAngleMode(CalcAngleMode m) { _angle = m; }
    CalcAngleMode angleMode() const { return _angle; }

    void setPrecision(uint8_t significantDigits);
    uint8_t precision() const { return _precision; }

    // True when the text is a maths expression, or clearly asks for one.
    static bool looksLikeExpression(const char* s);

    // Rewrite natural language into symbolic form. Returns false when nothing
    // evaluable remains.
    static bool extractExpression(const char* text, char* out, size_t outSize);

    // True when a prepared string contains only numbers, operators and names
    // the parser knows. Used to tell a maths question from ordinary text.
    static bool isCalculableExpression(const char* prepared);

    // Format a double the way this class formats results.
    static String formatNumber(double v, uint8_t significantDigits = 10);

private:
    CalcError _err;
    double _value;
    CalcAngleMode _angle;
    uint8_t _precision;

    char _buf[AMELTECH_CALC_MAX_EXPR];
    int _pos;
    int _len;
    uint8_t _depth;
    uint16_t _ops;

    bool parse(const char* expr);

    double parseExpr(bool* isBarePercent);
    double parseTerm(bool* isBarePercent);
    double parseUnary(bool* isBarePercent);
    double parsePower(bool* isBarePercent);
    double parsePostfix(bool* isBarePercent);
    double parsePrimary();
    double parseNumber();
    double callFunction(const char* name);

    void skipSpace();
    bool match(char c);
    char peek();
    char peekAhead(int n);
    char get();

    bool budget();               // op budget check
    bool enter();                // recursion depth guard
    void leave() { if (_depth) --_depth; }
    double applyAngleIn(double v) const;
    double applyAngleOut(double v) const;
    bool checkFinite(double v);
};

#endif // AMELTECH_CALCULATOR_H
