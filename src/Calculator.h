// =============================================================
// Calculator.h
//
// Safe embedded arithmetic expression evaluator. Supports:
//   + - * / % parentheses decimal numbers operator precedence
//   trailing "%" as percentage-of, and "X + Y%" style expressions
//
// Explicitly NOT a general-purpose expression evaluator: no
// variables, no function calls, no arbitrary code execution.
// =============================================================
#ifndef AMELTECH_CALCULATOR_H
#define AMELTECH_CALCULATOR_H

#include <Arduino.h>

enum CalcStatus {
    CALC_OK = 0,
    CALC_ERROR_SYNTAX,
    CALC_ERROR_DIV_BY_ZERO,
    CALC_ERROR_INVALID_CHAR,
    CALC_ERROR_UNSUPPORTED,
    CALC_ERROR_NON_FINITE,
    CALC_ERROR_TOO_LONG,
    CALC_ERROR_EMPTY
};

struct CalcResult {
    CalcStatus status;
    double value;
    bool valid;
    String message;
};

class Calculator {
public:
    Calculator();

    // Evaluates a bounded arithmetic expression string.
    CalcResult evaluate(const String& expression) const;

    static const size_t MAX_EXPRESSION_LEN = 64;

private:
    // Recursive-descent parser (bounded depth) over a validated
    // character set. All methods operate on a shared parse cursor.
    struct ParseState {
        const char* text;
        size_t pos;
        size_t len;
        bool error;
        CalcStatus errorStatus;
        String errorMessage;
        uint8_t depth;
    };

    static const uint8_t MAX_PAREN_DEPTH = 12;

    static bool _validateCharset(const String& expr, String& outError);

    static double _parseExpression(ParseState& st);
    static double _parseTerm(ParseState& st);
    static double _parsePercentSuffix(ParseState& st, double value);
    static double _parseFactor(ParseState& st);
    static double _parseNumber(ParseState& st);
    static void _skipSpaces(ParseState& st);
    static void _setError(ParseState& st, CalcStatus status, const String& msg);
};

#endif // AMELTECH_CALCULATOR_H
