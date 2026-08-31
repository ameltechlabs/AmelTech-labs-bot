#include "Calculator.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

// ---------------------------------------------------------------------------
Calculator::Calculator()
    : _err(CALC_OK),
      _value(0.0),
      _angle(CALC_RADIANS),
      _precision(10),
      _pos(0),
      _len(0),
      _depth(0),
      _ops(0) {
    _buf[0] = '\0';
}

void Calculator::setPrecision(uint8_t significantDigits) {
    if (significantDigits < 1) significantDigits = 1;
    if (significantDigits > 15) significantDigits = 15;
    _precision = significantDigits;
}

const char* Calculator::lastErrorString() const {
    switch (_err) {
        case CALC_OK:           return "OK";
        case CALC_DIV_ZERO:     return "division by zero";
        case CALC_MALFORMED:    return "malformed expression";
        case CALC_OVERFLOW:     return "result is too large or not a number";
        case CALC_INVALID_CHAR: return "unsupported character";
        case CALC_EMPTY:        return "empty expression";
        case CALC_PAREN:        return "mismatched parentheses";
        case CALC_UNKNOWN_FUNC: return "unknown function or constant";
        case CALC_DOMAIN:       return "value outside the function's domain";
        case CALC_TOO_LONG:     return "expression is too long";
        case CALC_TOO_DEEP:     return "expression is nested too deeply";
        case CALC_TOO_COMPLEX:  return "expression needs too many operations";
        case CALC_ARITY:        return "wrong number of arguments";
        default:                return "unknown error";
    }
}

// ---------------------------------------------------------------------------
// Natural-language rewriting
// ---------------------------------------------------------------------------
struct CalcPhrase {
    const char* from;
    const char* to;
};

// Order matters: longer phrases must come first.
static const CalcPhrase CALC_PHRASES[] = {
    // polite / interrogative prefixes are stripped by leading-phrase removal
    {"multiplied by", "*"},
    {"multiply by", "*"},
    {"divided by", "/"},
    {"divide by", "/"},
    {"to the power of", "^"},
    {"to the power", "^"},
    {"raised to the", "^"},
    {"raised to", "^"},
    {"power of", "^"},
    {"percent of", "%*"},
    {"per cent of", "%*"},
    {"square root of", " sqrt#"},
    {"squareroot of", " sqrt#"},
    {"square root", " sqrt#"},
    {"cube root of", " cbrt#"},
    {"cuberoot of", " cbrt#"},
    {"absolute value of", " abs#"},
    {"factorial of", " fact#"},
    {"remainder of", "%"},
    {"modulo", "%"},
    {"the sum of", "+"},
    {"sum of", "+"},
    {"difference of", "-"},
    {"product of", "*"},
    {"squared", "^2"},
    {"cubed", "^3"},
    {"percent", "%"},
    {"per cent", "%"},
    {"plus", "+"},
    {"minus", "-"},
    {"times", "*"},
    {"divide", "/"},
    {"over", "/"},
    {"mod", "%"},
    {"add", "+"},
    {"and", "+"},
    {"into", "*"},
    {nullptr, nullptr}
};

static const char* const CALC_LEAD_PHRASES[] = {
    "can you calculate", "can you compute", "can you solve", "could you calculate",
    "please calculate", "please compute", "please solve",
    "what is the value of", "what is the result of", "what is the answer to",
    "how much is", "how many is", "what is", "whats", "what's",
    "calculate", "compute", "evaluate", "solve", "tell me", "please", "answer",
    "result of", "value of", "equals", "equal to",
    nullptr
};

static bool calcIsWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Word-boundary aware in-place replacement.
static void calcReplace(char* buf, size_t bufSize, const char* from, const char* to,
                        bool wordBoundary) {
    size_t fl = strlen(from);
    size_t tl = strlen(to);
    if (fl == 0) return;
    char* p = buf;
    while ((p = strstr(p, from)) != nullptr) {
        if (wordBoundary) {
            bool leftOk = (p == buf) || !calcIsWordChar(*(p - 1));
            char after = *(p + fl);
            bool rightOk = (after == '\0') || !calcIsWordChar(after);
            // Never rewrite something that is being called as a function:
            // "mod(10,3)" must stay a call, not become "%(10,3)".
            const char* look = p + fl;
            while (*look == ' ') ++look;
            if (*look == '(') { ++p; continue; }
            if (!leftOk || !rightOk) { ++p; continue; }
        }
        size_t head = (size_t)(p - buf);
        size_t tail = strlen(p + fl);
        if (head + tl + tail >= bufSize) return;
        memmove(p + tl, p + fl, tail + 1);
        memcpy(p, to, tl);
        p += tl;
    }
}

// Turn " sqrt#144" (and friends) into "sqrt(144)".
static void calcWrapFunctionMarkers(char* buf, size_t bufSize) {
    for (;;) {
        char* hash = strchr(buf, '#');
        if (!hash) return;

        // Find the start of the function name immediately before '#'.
        char* nameEnd = hash;
        char* nameStart = nameEnd;
        while (nameStart > buf && calcIsWordChar(*(nameStart - 1))) --nameStart;

        char* arg = hash + 1;
        while (*arg == ' ') ++arg;

        // Determine the extent of the argument: a parenthesised group, or a
        // run of number/identifier characters.
        char* argEnd = arg;
        if (*arg == '(') {
            int depth = 0;
            while (*argEnd) {
                if (*argEnd == '(') ++depth;
                else if (*argEnd == ')') {
                    --depth;
                    if (depth == 0) { ++argEnd; break; }
                }
                ++argEnd;
            }
        } else {
            if (*argEnd == '-' || *argEnd == '+') ++argEnd;
            while (*argEnd && (calcIsWordChar(*argEnd) || *argEnd == '.')) ++argEnd;
        }

        if (argEnd == arg) {
            // Nothing to wrap: drop the marker so parsing can fail cleanly.
            memmove(hash, hash + 1, strlen(hash + 1) + 1);
            continue;
        }

        size_t nameLen = (size_t)(nameEnd - nameStart);
        size_t argLen = (size_t)(argEnd - arg);
        char name[16];
        char argText[64];
        if (nameLen >= sizeof(name)) nameLen = sizeof(name) - 1;
        if (argLen >= sizeof(argText)) argLen = sizeof(argText) - 1;
        memcpy(name, nameStart, nameLen);
        name[nameLen] = '\0';
        memcpy(argText, arg, argLen);
        argText[argLen] = '\0';

        char replacement[96];
        bool alreadyParen = (argText[0] == '(');
        snprintf(replacement, sizeof(replacement), "%s%s%s%s",
                 name,
                 alreadyParen ? "" : "(",
                 argText,
                 alreadyParen ? "" : ")");

        size_t head = (size_t)(nameStart - buf);
        size_t tailLen = strlen(argEnd);
        size_t rl = strlen(replacement);
        if (head + rl + tailLen >= bufSize) {
            memmove(hash, hash + 1, strlen(hash + 1) + 1);
            continue;
        }
        memmove(nameStart + rl, argEnd, tailLen + 1);
        memcpy(nameStart, replacement, rl);
    }
}

bool Calculator::extractExpression(const char* text, char* out, size_t outSize) {
    if (!text || !out || outSize < 8) {
        if (out && outSize) out[0] = '\0';
        return false;
    }

    // Lowercase copy, dropping characters that never belong in maths.
    char work[AMELTECH_CALC_MAX_EXPR * 2];
    size_t j = 0;
    for (const char* p = text; *p && j < sizeof(work) - 1; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == '?' || c == ',' ) {
            // Commas separate function arguments; keep them, drop question marks.
            if (c == ',') work[j++] = ',';
            continue;
        }
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        if (c == 'x' ) {
            // "12 x 4" means multiply; "max(" must survive.
            bool leftSpace = (j == 0) || work[j - 1] == ' ';
            char next = *(p + 1);
            bool rightSpace = (next == ' ' || next == '\0');
            if (leftSpace && rightSpace) { work[j++] = '*'; continue; }
        }
        work[j++] = c;
    }
    work[j] = '\0';

    // Unicode operators arrive as UTF-8; map the common ones.
    calcReplace(work, sizeof(work), "\xc3\x97", "*", false);   // ×
    calcReplace(work, sizeof(work), "\xc3\xb7", "/", false);   // ÷
    calcReplace(work, sizeof(work), "\xe2\x88\x92", "-", false); // −

    // Strip leading conversational phrases, repeatedly.
    bool stripped = true;
    while (stripped) {
        stripped = false;
        char* s = work;
        while (*s == ' ') ++s;
        if (s != work) {
            memmove(work, s, strlen(s) + 1);
        }
        for (int i = 0; CALC_LEAD_PHRASES[i]; ++i) {
            size_t pl = strlen(CALC_LEAD_PHRASES[i]);
            if (strncmp(work, CALC_LEAD_PHRASES[i], pl) == 0) {
                char after = work[pl];
                if (after == '\0' || after == ' ' || !calcIsWordChar(after)) {
                    memmove(work, work + pl, strlen(work + pl) + 1);
                    stripped = true;
                    break;
                }
            }
        }
    }

    for (int i = 0; CALC_PHRASES[i].from; ++i) {
        calcReplace(work, sizeof(work), CALC_PHRASES[i].from, CALC_PHRASES[i].to, true);
    }
    calcWrapFunctionMarkers(work, sizeof(work));

    // The symbol form "15% of 200" needs the same treatment the word form
    // ("15 percent of 200") already gets, otherwise the "of" is dropped and
    // the 200 is left stranded with no operator.
    calcReplace(work, sizeof(work), "% of", "%*", false);

    // " of " after a percent already became "%*"; any remaining "of" is a
    // multiplication in phrases like "half of 10" which we do not support, so
    // it is simply removed as a separator.
    calcReplace(work, sizeof(work), "of", " ", true);
    calcReplace(work, sizeof(work), "=", " ", false);

    // Collapse whitespace and copy out.
    size_t k = 0;
    bool lastSpace = true;
    for (size_t i = 0; work[i] && k < outSize - 1; ++i) {
        char c = work[i];
        if (c == ' ') {
            if (lastSpace) continue;
            lastSpace = true;
            out[k++] = ' ';
        } else {
            lastSpace = false;
            out[k++] = c;
        }
    }
    while (k > 0 && out[k - 1] == ' ') --k;
    out[k] = '\0';

    // Final gate. The previous release accepted anything containing a letter,
    // which meant a plain question such as "what is wifi" was handed to the
    // parser and came back as "unknown function". Now every alphabetic run in
    // the result must be a name the calculator actually knows, and there must
    // be a number or a named constant to work with.
    return isCalculableExpression(out);
}

// Names the parser understands. Anything else means the text is not maths.
static const char* const CALC_KNOWN_NAMES[] = {
    "sqrt", "cbrt", "abs", "sq", "sign", "exp", "ln", "log", "log2", "log10",
    "floor", "ceil", "round", "trunc", "sin", "cos", "tan", "asin", "acos",
    "atan", "atan2", "sinh", "cosh", "tanh", "deg", "rad", "fact", "pow",
    "min", "max", "mod", "hypot", "gcd", "lcm",
    "pi", "e", "tau", "phi",
    nullptr
};

static bool calcNameIsKnown(const char* name) {
    for (int i = 0; CALC_KNOWN_NAMES[i]; ++i) {
        if (strcmp(CALC_KNOWN_NAMES[i], name) == 0) return true;
    }
    return false;
}

bool Calculator::isCalculableExpression(const char* s) {
    if (!s || !s[0]) return false;

    bool hasDigit = false;
    bool hasStandaloneConstant = false;
    bool hasOperator = false;

    size_t i = 0;
    while (s[i]) {
        char c = s[i];

        if (c >= '0' && c <= '9') { hasDigit = true; ++i; continue; }

        // Scientific notation: the "e" in 1.5e3 is an exponent marker, not
        // the constant e. It only counts as one directly after a digit or a
        // decimal point and directly before a digit or a sign.
        if ((c == 'e') && i > 0 &&
            ((s[i - 1] >= '0' && s[i - 1] <= '9') || s[i - 1] == '.')) {
            size_t j = i + 1;
            if (s[j] == '+' || s[j] == '-') ++j;
            if (s[j] >= '0' && s[j] <= '9') {
                while (s[j] >= '0' && s[j] <= '9') ++j;
                i = j;
                continue;
            }
        }

        if (c >= 'a' && c <= 'z') {
            // Read the identifier, digits included so log2 stays one name.
            char name[16];
            size_t n = 0;
            while (s[i] && ((s[i] >= 'a' && s[i] <= 'z') ||
                            (n > 0 && s[i] >= '0' && s[i] <= '9'))) {
                if (n < sizeof(name) - 1) name[n++] = s[i];
                ++i;
            }
            name[n] = '\0';
            if (!calcNameIsKnown(name)) return false;
            // A one letter constant such as "e" is far too easy to hit by
            // accident, so it never validates an expression on its own.
            if (n > 1 && (!strcmp(name, "pi") || !strcmp(name, "tau") ||
                          !strcmp(name, "phi"))) {
                hasStandaloneConstant = true;
            }
            continue;
        }

        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '^' || c == '!' || c == '|') {
            hasOperator = true;
        }
        ++i;
    }

    (void)hasOperator;
    return hasDigit || hasStandaloneConstant;
}

bool Calculator::looksLikeExpression(const char* s) {
    if (!s || !s[0]) return false;

    // Pure symbolic form: digits plus at least one operator or parenthesis.
    bool onlySymbols = true;
    bool hasDigit = false;
    bool hasOp = false;
    for (const char* p = s; *p; ++p) {
        char c = *p;
        if (c >= '0' && c <= '9') { hasDigit = true; continue; }
        if (c == '.' || c == ' ' || c == '\t') continue;
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '^' || c == '!' || c == '(' || c == ')' || c == '|' || c == ',') {
            hasOp = true;
            continue;
        }
        onlySymbols = false;
    }
    if (onlySymbols && hasDigit && hasOp) return true;

    // Natural-language form: a maths verb plus a number.
    static const char* const triggers[] = {
        "plus", "minus", "times", "multiplied", "multiply", "divided", "divide",
        "square root", "squareroot", "cube root", "squared", "cubed", "percent",
        "power of", "raised to", "factorial", "modulo", " mod ", "sum of",
        "product of", "sqrt", "calculate", "compute", nullptr
    };
    char low[160];
    size_t n = 0;
    for (const char* p = s; *p && n < sizeof(low) - 1; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        low[n++] = c;
    }
    low[n] = '\0';

    bool anyDigit = false;
    for (size_t i = 0; low[i]; ++i) {
        if (low[i] >= '0' && low[i] <= '9') { anyDigit = true; break; }
    }
    if (!anyDigit) return false;

    for (int i = 0; triggers[i]; ++i) {
        if (strstr(low, triggers[i])) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Lexer helpers
// ---------------------------------------------------------------------------
void Calculator::skipSpace() {
    while (_pos < _len && (_buf[_pos] == ' ' || _buf[_pos] == '\t')) ++_pos;
}

char Calculator::peek() {
    skipSpace();
    if (_pos >= _len) return '\0';
    return _buf[_pos];
}

char Calculator::peekAhead(int n) {
    int p = _pos;
    while (p < _len && (_buf[p] == ' ' || _buf[p] == '\t')) ++p;
    p += n;
    while (p < _len && (_buf[p] == ' ' || _buf[p] == '\t')) ++p;
    if (p >= _len) return '\0';
    return _buf[p];
}

char Calculator::get() {
    skipSpace();
    if (_pos >= _len) return '\0';
    return _buf[_pos++];
}

bool Calculator::match(char c) {
    if (peek() == c) { get(); return true; }
    return false;
}

bool Calculator::budget() {
    if (++_ops > AMELTECH_CALC_MAX_OPS) {
        if (_err == CALC_OK) _err = CALC_TOO_COMPLEX;
        return false;
    }
    return true;
}

bool Calculator::enter() {
    if (++_depth > AMELTECH_CALC_MAX_DEPTH) {
        if (_err == CALC_OK) _err = CALC_TOO_DEEP;
        return false;
    }
    return true;
}

bool Calculator::checkFinite(double v) {
    if (!isfinite(v) || fabs(v) > 1e300) {
        if (_err == CALC_OK) _err = CALC_OVERFLOW;
        return false;
    }
    return true;
}

double Calculator::applyAngleIn(double v) const {
    return (_angle == CALC_DEGREES) ? v * M_PI / 180.0 : v;
}

double Calculator::applyAngleOut(double v) const {
    return (_angle == CALC_DEGREES) ? v * 180.0 / M_PI : v;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
double Calculator::parseNumber() {
    skipSpace();
    int start = _pos;
    bool sawDigit = false;
    bool sawDot = false;

    while (_pos < _len) {
        char c = _buf[_pos];
        if (c >= '0' && c <= '9') { sawDigit = true; ++_pos; }
        else if (c == '.' && !sawDot) { sawDot = true; ++_pos; }
        else break;
    }
    // Scientific notation: 1e6, 2.5e-3
    if (sawDigit && _pos < _len && (_buf[_pos] == 'e' || _buf[_pos] == 'E')) {
        int save = _pos;
        ++_pos;
        if (_pos < _len && (_buf[_pos] == '+' || _buf[_pos] == '-')) ++_pos;
        bool expDigit = false;
        while (_pos < _len && _buf[_pos] >= '0' && _buf[_pos] <= '9') { expDigit = true; ++_pos; }
        if (!expDigit) _pos = save;
    }

    if (!sawDigit) {
        _err = CALC_MALFORMED;
        return 0.0;
    }

    char tmp[48];
    int n = _pos - start;
    if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
    memcpy(tmp, _buf + start, (size_t)n);
    tmp[n] = '\0';

    char* endp = nullptr;
    double v = strtod(tmp, &endp);
    if (endp == tmp) {
        _err = CALC_MALFORMED;
        return 0.0;
    }
    return v;
}

static double calcFactorial(double x, CalcError& err) {
    if (x < 0.0 || fabs(x - floor(x + 0.5)) > 1e-9) {
        err = CALC_DOMAIN;
        return 0.0;
    }
    long n = (long)llround(x);
    if (n > 170) {
        err = CALC_OVERFLOW;
        return 0.0;
    }
    double r = 1.0;
    for (long i = 2; i <= n; ++i) r *= (double)i;
    return r;
}

static double calcGcd(double a, double b) {
    long x = (long)llround(fabs(a));
    long y = (long)llround(fabs(b));
    while (y != 0) {
        long t = x % y;
        x = y;
        y = t;
    }
    return (double)x;
}

double Calculator::callFunction(const char* name) {
    // Two-argument functions.
    static const char* const two[] = {"pow", "min", "max", "mod", "hypot",
                                      "atan2", "gcd", "lcm", nullptr};
    bool isTwo = false;
    for (int i = 0; two[i]; ++i) {
        if (strcmp(name, two[i]) == 0) { isTwo = true; break; }
    }

    if (!match('(')) {
        _err = CALC_MALFORMED;
        return 0.0;
    }
    bool dummy = false;
    double a = parseExpr(&dummy);
    if (_err != CALC_OK) return 0.0;

    double b = 0.0;
    if (isTwo) {
        if (!match(',')) {
            _err = CALC_ARITY;
            return 0.0;
        }
        b = parseExpr(&dummy);
        if (_err != CALC_OK) return 0.0;
    }
    if (!match(')')) {
        _err = CALC_PAREN;
        return 0.0;
    }
    if (!budget()) return 0.0;

    double r = 0.0;
    if (!strcmp(name, "sqrt")) {
        if (a < 0.0) { _err = CALC_DOMAIN; return 0.0; }
        r = sqrt(a);
    } else if (!strcmp(name, "cbrt")) {
        r = cbrt(a);
    } else if (!strcmp(name, "abs")) {
        r = fabs(a);
    } else if (!strcmp(name, "sq")) {
        r = a * a;
    } else if (!strcmp(name, "sign")) {
        r = (a > 0.0) ? 1.0 : ((a < 0.0) ? -1.0 : 0.0);
    } else if (!strcmp(name, "exp")) {
        r = exp(a);
    } else if (!strcmp(name, "ln")) {
        if (a <= 0.0) { _err = CALC_DOMAIN; return 0.0; }
        r = log(a);
    } else if (!strcmp(name, "log") || !strcmp(name, "log10")) {
        if (a <= 0.0) { _err = CALC_DOMAIN; return 0.0; }
        r = log10(a);
    } else if (!strcmp(name, "log2")) {
        if (a <= 0.0) { _err = CALC_DOMAIN; return 0.0; }
        r = log(a) / log(2.0);
    } else if (!strcmp(name, "floor")) {
        r = floor(a);
    } else if (!strcmp(name, "ceil")) {
        r = ceil(a);
    } else if (!strcmp(name, "round")) {
        r = (a < 0.0) ? -floor(-a + 0.5) : floor(a + 0.5);
    } else if (!strcmp(name, "sin")) {
        r = sin(applyAngleIn(a));
    } else if (!strcmp(name, "cos")) {
        r = cos(applyAngleIn(a));
    } else if (!strcmp(name, "tan")) {
        double t = applyAngleIn(a);
        double c = cos(t);
        if (fabs(c) < 1e-12) { _err = CALC_DOMAIN; return 0.0; }
        r = sin(t) / c;
    } else if (!strcmp(name, "asin")) {
        if (a < -1.0 || a > 1.0) { _err = CALC_DOMAIN; return 0.0; }
        r = applyAngleOut(asin(a));
    } else if (!strcmp(name, "acos")) {
        if (a < -1.0 || a > 1.0) { _err = CALC_DOMAIN; return 0.0; }
        r = applyAngleOut(acos(a));
    } else if (!strcmp(name, "atan")) {
        r = applyAngleOut(atan(a));
    } else if (!strcmp(name, "sinh")) {
        r = sinh(a);
    } else if (!strcmp(name, "cosh")) {
        r = cosh(a);
    } else if (!strcmp(name, "tanh")) {
        r = tanh(a);
    } else if (!strcmp(name, "deg")) {
        r = a * 180.0 / M_PI;
    } else if (!strcmp(name, "rad")) {
        r = a * M_PI / 180.0;
    } else if (!strcmp(name, "fact")) {
        r = calcFactorial(a, _err);
        if (_err != CALC_OK) return 0.0;
    } else if (!strcmp(name, "pow")) {
        r = pow(a, b);
    } else if (!strcmp(name, "min")) {
        r = a < b ? a : b;
    } else if (!strcmp(name, "max")) {
        r = a > b ? a : b;
    } else if (!strcmp(name, "mod")) {
        if (b == 0.0) { _err = CALC_DIV_ZERO; return 0.0; }
        r = fmod(a, b);
    } else if (!strcmp(name, "hypot")) {
        r = sqrt(a * a + b * b);
    } else if (!strcmp(name, "atan2")) {
        r = applyAngleOut(atan2(a, b));
    } else if (!strcmp(name, "gcd")) {
        r = calcGcd(a, b);
    } else if (!strcmp(name, "lcm")) {
        double g = calcGcd(a, b);
        if (g == 0.0) { r = 0.0; }
        else r = fabs(a * b) / g;
    } else {
        _err = CALC_UNKNOWN_FUNC;
        return 0.0;
    }

    if (!checkFinite(r)) return 0.0;
    return r;
}

double Calculator::parsePrimary() {
    skipSpace();
    char c = peek();

    if (c == '\0') {
        _err = CALC_MALFORMED;
        return 0.0;
    }

    if (c == '(') {
        get();
        if (!enter()) return 0.0;
        bool dummy = false;
        double v = parseExpr(&dummy);
        leave();
        if (_err != CALC_OK) return 0.0;
        if (!match(')')) {
            _err = CALC_PAREN;
            return 0.0;
        }
        return v;
    }

    if (c == '|') {
        get();
        if (!enter()) return 0.0;
        bool dummy = false;
        double v = parseExpr(&dummy);
        leave();
        if (_err != CALC_OK) return 0.0;
        if (!match('|')) {
            _err = CALC_PAREN;
            return 0.0;
        }
        return fabs(v);
    }

    if ((c >= 'a' && c <= 'z')) {
        char name[16];
        size_t n = 0;
        skipSpace();
        while (_pos < _len && _buf[_pos] >= 'a' && _buf[_pos] <= 'z') {
            if (n < sizeof(name) - 1) name[n++] = _buf[_pos];
            ++_pos;
        }
        // digits inside identifiers (log2) are part of the name
        while (_pos < _len && _buf[_pos] >= '0' && _buf[_pos] <= '9' && n > 0) {
            if (n < sizeof(name) - 1) name[n++] = _buf[_pos];
            ++_pos;
        }
        name[n] = '\0';

        if (peek() == '(') {
            if (!enter()) return 0.0;
            double v = callFunction(name);
            leave();
            return v;
        }
        if (!strcmp(name, "pi")) return M_PI;
        if (!strcmp(name, "e")) return M_E;
        if (!strcmp(name, "tau")) return 2.0 * M_PI;
        if (!strcmp(name, "phi")) return 1.618033988749894848;
        _err = CALC_UNKNOWN_FUNC;
        return 0.0;
    }

    return parseNumber();
}

double Calculator::parsePostfix(bool* isBarePercent) {
    double v = parsePrimary();
    if (_err != CALC_OK) return 0.0;
    bool pct = false;

    for (;;) {
        skipSpace();
        char c = peek();
        if (c == '!') {
            get();
            if (!budget()) return 0.0;
            v = calcFactorial(v, _err);
            if (_err != CALC_OK) return 0.0;
            pct = false;
            continue;
        }
        if (c == '%') {
            // '%' is modulo when a value clearly follows, percent otherwise.
            char nxt = peekAhead(1);
            bool valueFollows = (nxt >= '0' && nxt <= '9') || nxt == '(' || nxt == '.';
            if (valueFollows) break;   // leave it for parseTerm as modulo
            get();
            if (!budget()) return 0.0;
            v = v / 100.0;
            pct = true;
            continue;
        }
        break;
    }

    if (isBarePercent) *isBarePercent = pct;
    if (!checkFinite(v)) return 0.0;
    return v;
}

double Calculator::parseUnary(bool* isBarePercent) {
    skipSpace();
    if (match('+')) return parseUnary(isBarePercent);
    if (match('-')) {
        if (!enter()) return 0.0;
        bool p = false;
        double v = parseUnary(&p);
        leave();
        if (isBarePercent) *isBarePercent = p;
        return -v;
    }
    return parsePower(isBarePercent);
}

// power := postfix ('^' unary)?
// Right associative, and binding tighter than unary minus, so -2^2 is -4 and
// 2^3^2 is 512, matching normal mathematical convention.
double Calculator::parsePower(bool* isBarePercent) {
    bool pct = false;
    double base = parsePostfix(&pct);
    if (_err != CALC_OK) return 0.0;

    skipSpace();
    if (peek() == '^') {
        get();
        if (!enter()) return 0.0;
        bool rp = false;
        double ex = parseUnary(&rp);
        leave();
        if (_err != CALC_OK) return 0.0;
        if (!budget()) return 0.0;

        // Negative base with a fractional exponent has no real result.
        if (base < 0.0 && fabs(ex - floor(ex + 0.5)) > 1e-9) {
            _err = CALC_DOMAIN;
            return 0.0;
        }
        base = pow(base, ex);
        pct = false;
        if (!checkFinite(base)) return 0.0;
    }

    if (isBarePercent) *isBarePercent = pct;
    return base;
}

double Calculator::parseTerm(bool* isBarePercent) {
    bool leftPct = false;
    double left = parseUnary(&leftPct);
    if (_err != CALC_OK) return 0.0;
    bool bare = leftPct;

    for (;;) {
        skipSpace();
        char op = peek();
        bool implicit = false;

        if (op != '*' && op != '/' && op != '%') {
            // Implicit multiplication: "3(4+5)", "2pi", "2 sqrt(9)".
            if (op == '(' || (op >= 'a' && op <= 'z')) {
                implicit = true;
                op = '*';
            } else {
                break;
            }
        }
        if (!implicit) get();

        bool rp = false;
        double right = parseUnary(&rp);
        if (_err != CALC_OK) return 0.0;
        if (!budget()) return 0.0;

        if (op == '*') {
            left *= right;
        } else if (op == '/') {
            if (right == 0.0) { _err = CALC_DIV_ZERO; return 0.0; }
            left /= right;
        } else {
            if (right == 0.0) { _err = CALC_DIV_ZERO; return 0.0; }
            left = fmod(left, right);
        }
        bare = false;
        if (!checkFinite(left)) return 0.0;
    }

    if (isBarePercent) *isBarePercent = bare;
    return left;
}

double Calculator::parseExpr(bool* isBarePercent) {
    bool leftPct = false;
    double left = parseTerm(&leftPct);
    if (_err != CALC_OK) return 0.0;
    bool bare = leftPct;

    for (;;) {
        skipSpace();
        char op = peek();
        if (op != '+' && op != '-') break;
        get();
        bool rightPct = false;
        double right = parseTerm(&rightPct);
        if (_err != CALC_OK) return 0.0;
        if (!budget()) return 0.0;

        // "200 + 10%" means 10 percent OF 200, which is what people expect.
        if (rightPct) right = left * right;

        if (op == '+') left += right;
        else left -= right;
        bare = false;
        if (!checkFinite(left)) return 0.0;
    }

    if (isBarePercent) *isBarePercent = bare;
    return left;
}

// ---------------------------------------------------------------------------
bool Calculator::parse(const char* expr) {
    _err = CALC_OK;
    _value = 0.0;
    _pos = 0;
    _len = 0;
    _depth = 0;
    _ops = 0;
    _buf[0] = '\0';

    if (!expr) { _err = CALC_EMPTY; return false; }

    char prepared[AMELTECH_CALC_MAX_EXPR];
    if (!extractExpression(expr, prepared, sizeof(prepared))) {
        _err = CALC_EMPTY;
        return false;
    }

    // The previous release silently truncated over-long expressions and then
    // evaluated the fragment. Refuse instead.
    if (strlen(prepared) >= AMELTECH_CALC_MAX_EXPR - 1) {
        _err = CALC_TOO_LONG;
        return false;
    }

    int i = 0;
    for (const char* p = prepared; *p; ++p) {
        char c = *p;
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  c == '.' || c == '+' || c == '-' || c == '*' || c == '/' ||
                  c == '%' || c == '(' || c == ')' || c == '^' || c == '!' ||
                  c == '|' || c == ',' || c == ' ';
        if (!ok) {
            _err = CALC_INVALID_CHAR;
            return false;
        }
        if (i >= AMELTECH_CALC_MAX_EXPR - 1) {
            _err = CALC_TOO_LONG;
            return false;
        }
        _buf[i++] = c;
    }
    _buf[i] = '\0';
    _len = i;

    if (_len == 0) { _err = CALC_EMPTY; return false; }

    // Balanced parentheses check up front gives a clearer error than failing
    // deep inside the recursive descent.
    int depth = 0;
    for (int k = 0; k < _len; ++k) {
        if (_buf[k] == '(') ++depth;
        else if (_buf[k] == ')') {
            --depth;
            if (depth < 0) { _err = CALC_PAREN; return false; }
        }
    }
    if (depth != 0) { _err = CALC_PAREN; return false; }

    bool dummy = false;
    _value = parseExpr(&dummy);
    if (_err != CALC_OK) return false;

    skipSpace();
    if (_pos < _len) {
        _err = CALC_MALFORMED;
        return false;
    }
    if (!checkFinite(_value)) return false;
    return true;
}

String Calculator::formatNumber(double v, uint8_t significantDigits) {
    char out[48];
    if (significantDigits < 1) significantDigits = 1;
    if (significantDigits > 15) significantDigits = 15;

    if (fabs(v) < 1e-12) {
        return String("0");
    }
    double r = (v < 0.0) ? -floor(-v + 0.5) : floor(v + 0.5);
    if (fabs(v - r) < 1e-9 && fabs(v) < 1e15) {
        snprintf(out, sizeof(out), "%.0f", r);
        return String(out);
    }
    snprintf(out, sizeof(out), "%.*g", (int)significantDigits, v);

    // Trim trailing zeros in the fractional part for readability.
    char* dot = strchr(out, '.');
    if (dot && !strchr(out, 'e') && !strchr(out, 'E')) {
        size_t len = strlen(out);
        while (len > 1 && out[len - 1] == '0') out[--len] = '\0';
        if (len > 1 && out[len - 1] == '.') out[--len] = '\0';
    }
    return String(out);
}

bool Calculator::evaluateTo(const char* expression, double& out) {
    if (!parse(expression)) return false;
    out = _value;
    return true;
}

String Calculator::evaluate(const char* expression) {
    if (!parse(expression)) return String("");
    return formatNumber(_value, _precision);
}

String Calculator::evaluate(const String& expression) {
    return evaluate(expression.c_str());
}
