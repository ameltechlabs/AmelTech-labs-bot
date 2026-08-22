// =============================================================
// Calculator.cpp
//
// Grammar (recursive descent, precedence climbing):
//   expression := term (('+' | '-') term)*
//   term       := factor (('*' | '/' | '%') factor)*
//   factor     := ['-'] primary [percentSuffix]
//   primary    := number | '(' expression ')'
//   percentSuffix := '%'      -- converts preceding value to value/100
//
// "25 + 10%" is interpreted as 25 + (25 * 0.10) i.e. "10% of 25 added
// to 25" ONLY when '%' directly follows a term in an additive context;
// a bare "50%" evaluates to 0.5. This mirrors common calculator-app
// behavior while remaining fully deterministic.
// =============================================================
#include "Calculator.h"
#include <math.h>

Calculator::Calculator() {}

void Calculator::_skipSpaces(ParseState& st) {
    while (st.pos < st.len && st.text[st.pos] == ' ') st.pos++;
}

void Calculator::_setError(ParseState& st, CalcStatus status, const String& msg) {
    if (!st.error) { // keep first error
        st.error = true;
        st.errorStatus = status;
        st.errorMessage = msg;
    }
}

bool Calculator::_validateCharset(const String& expr, String& outError) {
    for (size_t i = 0; i < expr.length(); i++) {
        char c = expr[i];
        bool ok = isdigit((unsigned char)c) || c == '.' || c == '+' || c == '-' ||
                  c == '*' || c == '/' || c == '%' || c == '(' || c == ')' || c == ' ';
        if (!ok) {
            outError = "Invalid character '" + String(c) + "' in expression.";
            return false;
        }
    }
    return true;
}

double Calculator::_parseNumber(ParseState& st) {
    _skipSpaces(st);
    size_t start = st.pos;
    bool sawDigit = false;
    bool sawDot = false;
    while (st.pos < st.len) {
        char c = st.text[st.pos];
        if (isdigit((unsigned char)c)) {
            sawDigit = true;
            st.pos++;
        } else if (c == '.' && !sawDot) {
            sawDot = true;
            st.pos++;
        } else {
            break;
        }
    }
    if (!sawDigit) {
        _setError(st, CALC_ERROR_SYNTAX, "Expected a number.");
        return 0.0;
    }
    String numStr = String(st.text).substring(start, st.pos);
    return numStr.toDouble();
}

double Calculator::_parsePercentSuffix(ParseState& st, double value) {
    _skipSpaces(st);
    if (st.pos < st.len && st.text[st.pos] == '%') {
        st.pos++;
        return value / 100.0;
    }
    return value;
}

double Calculator::_parseFactor(ParseState& st) {
    _skipSpaces(st);
    if (st.error) return 0.0;

    bool negate = false;
    if (st.pos < st.len && (st.text[st.pos] == '-' || st.text[st.pos] == '+')) {
        negate = (st.text[st.pos] == '-');
        st.pos++;
        _skipSpaces(st);
    }

    double value;
    if (st.pos < st.len && st.text[st.pos] == '(') {
        st.depth++;
        if (st.depth > MAX_PAREN_DEPTH) {
            _setError(st, CALC_ERROR_SYNTAX, "Expression nesting too deep.");
            return 0.0;
        }
        st.pos++; // consume '('
        value = _parseExpression(st);
        _skipSpaces(st);
        if (st.pos >= st.len || st.text[st.pos] != ')') {
            _setError(st, CALC_ERROR_SYNTAX, "Missing closing parenthesis.");
            return 0.0;
        }
        st.pos++; // consume ')'
        st.depth--;
    } else {
        value = _parseNumber(st);
    }

    if (st.error) return 0.0;

    value = _parsePercentSuffix(st, value);
    if (negate) value = -value;
    return value;
}

double Calculator::_parseTerm(ParseState& st) {
    double value = _parseFactor(st);
    if (st.error) return 0.0;

    for (;;) {
        _skipSpaces(st);
        if (st.pos >= st.len) break;
        char op = st.text[st.pos];
        if (op != '*' && op != '/' && op != '%') break;
        st.pos++;
        double rhs = _parseFactor(st);
        if (st.error) return 0.0;

        if (op == '*') {
            value = value * rhs;
        } else if (op == '/') {
            if (rhs == 0.0) {
                _setError(st, CALC_ERROR_DIV_BY_ZERO, "Division by zero.");
                return 0.0;
            }
            value = value / rhs;
        } else { // '%' as modulo when used as a binary operator between two factors
            if (rhs == 0.0) {
                _setError(st, CALC_ERROR_DIV_BY_ZERO, "Modulo by zero.");
                return 0.0;
            }
            value = fmod(value, rhs);
        }

        if (!isfinite(value)) {
            _setError(st, CALC_ERROR_NON_FINITE, "Result is not a finite number.");
            return 0.0;
        }
    }
    return value;
}

double Calculator::_parseExpression(ParseState& st) {
    double value = _parseTerm(st);
    if (st.error) return 0.0;

    for (;;) {
        _skipSpaces(st);
        if (st.pos >= st.len) break;
        char op = st.text[st.pos];
        if (op != '+' && op != '-') break;
        st.pos++;
        double rhs = _parseTerm(st);
        if (st.error) return 0.0;

        value = (op == '+') ? (value + rhs) : (value - rhs);

        if (!isfinite(value)) {
            _setError(st, CALC_ERROR_NON_FINITE, "Result is not a finite number.");
            return 0.0;
        }
    }
    return value;
}

CalcResult Calculator::evaluate(const String& expression) const {
    CalcResult result;
    result.value = 0.0;
    result.valid = false;

    String expr = expression;
    expr.trim();

    if (expr.length() == 0) {
        result.status = CALC_ERROR_EMPTY;
        result.message = "Expression is empty.";
        return result;
    }
    if (expr.length() > MAX_EXPRESSION_LEN) {
        result.status = CALC_ERROR_TOO_LONG;
        result.message = "Expression exceeds maximum length of " + String(MAX_EXPRESSION_LEN) + " characters.";
        return result;
    }

    String charsetError;
    if (!_validateCharset(expr, charsetError)) {
        result.status = CALC_ERROR_INVALID_CHAR;
        result.message = charsetError;
        return result;
    }

    ParseState st;
    st.text = expr.c_str();
    st.pos = 0;
    st.len = expr.length();
    st.error = false;
    st.errorStatus = CALC_OK;
    st.depth = 0;

    double value = _parseExpression(st);

    if (!st.error) {
        _skipSpaces(st);
        if (st.pos != st.len) {
            _setError(st, CALC_ERROR_SYNTAX, "Unexpected trailing characters in expression.");
        }
    }

    if (st.error) {
        result.status = st.errorStatus;
        result.message = st.errorMessage;
        result.valid = false;
        return result;
    }

    if (!isfinite(value)) {
        result.status = CALC_ERROR_NON_FINITE;
        result.message = "Result is not a finite number.";
        result.valid = false;
        return result;
    }

    result.status = CALC_OK;
    result.value = value;
    result.valid = true;
    result.message = "OK";
    return result;
}
