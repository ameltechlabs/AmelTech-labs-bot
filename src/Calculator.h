/*
 * Safe embedded calculator
 * Supports + - * / % parentheses decimals operator precedence
 * Rejects division by zero, malformed input, non-finite results
 * No arbitrary code execution
 */

#ifndef AMELTECH_CALCULATOR_H
#define AMELTECH_CALCULATOR_H

#include <Arduino.h>

enum CalcError : int8_t {
    CALC_OK = 0,
    CALC_DIV_ZERO = -1,
    CALC_MALFORMED = -2,
    CALC_OVERFLOW = -3,
    CALC_INVALID_CHAR = -4,
    CALC_EMPTY = -5,
    CALC_PAREN = -6
};

class Calculator {
public:
    Calculator();

    // Evaluate expression. On success returns string of result.
    // On failure returns empty string and sets lastError().
    String evaluate(const String& expression);
    String evaluate(const char* expression);

    CalcError lastError() const { return _err; }
    const char* lastErrorString() const;
    double lastValue() const { return _value; }

    // Detect if string looks like a math expression
    static bool looksLikeExpression(const char* s);

private:
    CalcError _err;
    double _value;

    // Recursive descent parser with fixed buffers
    static const int MAX_EXPR = 96;
    char _buf[MAX_EXPR];
    int _pos;
    int _len;

    bool parse(const char* expr);
    double parseExpr();
    double parseTerm();
    double parseFactor();
    double parseNumber();
    void skipSpace();
    bool match(char c);
    char peek();
    char get();
};

#endif // AMELTECH_CALCULATOR_H
