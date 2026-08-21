#include "Calculator.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

Calculator::Calculator() : _err(CALC_OK), _value(0.0), _pos(0), _len(0) {
    _buf[0] = '\0';
}

const char* Calculator::lastErrorString() const {
    switch (_err) {
        case CALC_OK: return "OK";
        case CALC_DIV_ZERO: return "Division by zero";
        case CALC_MALFORMED: return "Malformed expression";
        case CALC_OVERFLOW: return "Overflow or non-finite result";
        case CALC_INVALID_CHAR: return "Invalid character";
        case CALC_EMPTY: return "Empty expression";
        case CALC_PAREN: return "Mismatched parentheses";
        default: return "Unknown error";
    }
}

bool Calculator::looksLikeExpression(const char* s) {
    if (!s || !s[0]) return false;
    bool hasDigit = false;
    bool hasOp = false;
    int paren = 0;
    for (const char* p = s; *p; ++p) {
        char c = *p;
        if (c >= '0' && c <= '9') hasDigit = true;
        else if (c == '.' || c == ' ' || c == '\t') continue;
        else if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%') hasOp = true;
        else if (c == '(') ++paren;
        else if (c == ')') --paren;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return false;
        else return false;
    }
    return hasDigit && (hasOp || paren != 0 || strchr(s, '%') != nullptr);
}

void Calculator::skipSpace() {
    while (_pos < _len && (_buf[_pos] == ' ' || _buf[_pos] == '\t')) ++_pos;
}

char Calculator::peek() {
    skipSpace();
    if (_pos >= _len) return '\0';
    return _buf[_pos];
}

char Calculator::get() {
    skipSpace();
    if (_pos >= _len) return '\0';
    return _buf[_pos++];
}

bool Calculator::match(char c) {
    if (peek() == c) {
        get();
        return true;
    }
    return false;
}

double Calculator::parseNumber() {
    skipSpace();
    int start = _pos;
    if (peek() == '+' || peek() == '-') {
        // unary handled at factor level usually; still allow
    }
    bool sawDigit = false;
    bool sawDot = false;
    while (_pos < _len) {
        char c = _buf[_pos];
        if (c >= '0' && c <= '9') {
            sawDigit = true;
            ++_pos;
        } else if (c == '.' && !sawDot) {
            sawDot = true;
            ++_pos;
        } else {
            break;
        }
    }
    if (!sawDigit) {
        _err = CALC_MALFORMED;
        return 0.0;
    }
    char tmp[32];
    int n = _pos - start;
    if (n >= (int)sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, _buf + start, n);
    tmp[n] = '\0';
    char* endp = nullptr;
    double v = strtod(tmp, &endp);
    if (endp == tmp) {
        _err = CALC_MALFORMED;
        return 0.0;
    }
    return v;
}

double Calculator::parseFactor() {
    skipSpace();
    if (match('+')) return parseFactor();
    if (match('-')) return -parseFactor();
    if (match('(')) {
        double v = parseExpr();
        if (!match(')')) {
            _err = CALC_PAREN;
            return 0.0;
        }
        // trailing percent after paren: (25+5)%
        if (match('%')) {
            v = v / 100.0;
        }
        return v;
    }
    double v = parseNumber();
    if (_err != CALC_OK) return 0.0;
    // postfix percent: 50%
    if (match('%')) {
        v = v / 100.0;
    }
    return v;
}

double Calculator::parseTerm() {
    double left = parseFactor();
    if (_err != CALC_OK) return 0.0;
    for (;;) {
        skipSpace();
        char op = peek();
        if (op != '*' && op != '/' && op != '%') break;
        get();
        double right = parseFactor();
        if (_err != CALC_OK) return 0.0;
        if (op == '*') {
            left *= right;
        } else if (op == '/') {
            if (right == 0.0) {
                _err = CALC_DIV_ZERO;
                return 0.0;
            }
            left /= right;
        } else {  // %
            if (right == 0.0) {
                _err = CALC_DIV_ZERO;
                return 0.0;
            }
            left = fmod(left, right);
        }
        if (!isfinite(left)) {
            _err = CALC_OVERFLOW;
            return 0.0;
        }
    }
    return left;
}

double Calculator::parseExpr() {
    double left = parseTerm();
    if (_err != CALC_OK) return 0.0;
    for (;;) {
        skipSpace();
        char op = peek();
        if (op != '+' && op != '-') break;
        get();
        double right = parseTerm();
        if (_err != CALC_OK) return 0.0;
        if (op == '+') left += right;
        else left -= right;
        if (!isfinite(left)) {
            _err = CALC_OVERFLOW;
            return 0.0;
        }
    }
    return left;
}

bool Calculator::parse(const char* expr) {
    _err = CALC_OK;
    _value = 0.0;
    if (!expr) {
        _err = CALC_EMPTY;
        return false;
    }
    // Copy and validate characters
    int i = 0;
    for (const char* p = expr; *p && i < MAX_EXPR - 1; ++p) {
        char c = *p;
        if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
            c == '*' || c == '/' || c == '%' || c == '(' || c == ')' ||
            c == ' ' || c == '\t') {
            _buf[i++] = c;
        } else {
            _err = CALC_INVALID_CHAR;
            return false;
        }
    }
    _buf[i] = '\0';
    _len = i;
    _pos = 0;
    if (_len == 0) {
        _err = CALC_EMPTY;
        return false;
    }

    // Special case: "25 + 10%" meaning 25 + 10% of 25 (common shorthand)
    // We support explicit "25 + 10%" as 25 + 0.10 = 25.10 via postfix %.
    // For "X + Y%" where user means X*(1+Y/100), they can write X*(1+Y/100).

    _value = parseExpr();
    if (_err != CALC_OK) return false;
    skipSpace();
    if (_pos < _len) {
        _err = CALC_MALFORMED;
        return false;
    }
    if (!isfinite(_value)) {
        _err = CALC_OVERFLOW;
        return false;
    }
    return true;
}

String Calculator::evaluate(const char* expression) {
    if (!parse(expression)) return String("");
    // Format reasonably
    char out[48];
    if (fabs(_value - round(_value)) < 1e-9 && fabs(_value) < 1e12) {
        snprintf(out, sizeof(out), "%.0f", _value);
    } else {
        snprintf(out, sizeof(out), "%.8g", _value);
    }
    return String(out);
}

String Calculator::evaluate(const String& expression) {
    return evaluate(expression.c_str());
}
