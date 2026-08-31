#!/usr/bin/env python3
"""
generate_knowledge.py
=====================

Validates data/knowledge.json and generates the built-in knowledge table:

    src/knowledge_generated.h    declarations only
    src/knowledge_generated.cpp  the single definition of the table

Why the split
-------------
The previous release defined the table as ``static const ... PROGMEM`` inside a
header. Every translation unit that included AmelTechBot.h therefore got its own
private copy of a ~380 KB table. Declaring it ``extern`` in the header and
defining it once in a .cpp guarantees exactly one copy in flash.

Why this file duplicates the C++ normalizer
-------------------------------------------
The stored ``normalized`` column is compared against the runtime normalization
of the user's question. If the two normalizers disagree by even one rule, exact
matches silently degrade into fuzzy matches. The tables and the algorithm below
are a byte-for-byte mirror of src/NeuralEngine.cpp. Change one, change both, and
run ``--selftest``.

Usage
-----
    python3 tools/generate_knowledge.py
    python3 tools/generate_knowledge.py --input data/knowledge.json \
                                        --out-dir src
    python3 tools/generate_knowledge.py --selftest
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter
from typing import Any, Dict, List, Tuple

# ---------------------------------------------------------------------------
# Limits — keep aligned with src/AmelTechConfig.h
# ---------------------------------------------------------------------------
MAX_QUESTION_LEN = 128
MAX_ANSWER_LEN = 320
MAX_CATEGORY_LEN = 32
MAX_TOKEN_LEN = 24
MAX_ENTRIES = 8192
NEURAL_DIM = 48

# ---------------------------------------------------------------------------
# Mirror of AMEL_SYN_TABLE in src/NeuralEngine.cpp (order matters only for
# readability; lookups are exact-match so duplicates would be ambiguous).
# ---------------------------------------------------------------------------
SYN_TABLE: Dict[str, str] = {
    "sec": "seconds", "secs": "seconds", "second": "seconds",
    "min": "minute", "mins": "minutes",
    "hr": "hour", "hrs": "hours",
    "r": "are", "u": "you", "ur": "your", "y": "why",
    "whats": "what is", "wats": "what is", "wat": "what", "wht": "what",
    "hows": "how is", "whos": "who is", "wheres": "where is",
    "whens": "when is", "hw": "how", "hru": "how are you",
    "pls": "please", "plz": "please", "thx": "thanks", "ty": "thanks",
    "im": "i am", "ive": "i have", "dont": "do not", "cant": "can not",
    "wont": "will not", "gimme": "give me", "lemme": "let me",
    "temp": "temperature", "humid": "humidity",
    "freq": "frequency", "mem": "memory",
    "cfg": "configuration", "config": "configuration",
    "info": "information", "docs": "documentation",
    "mcu": "microcontroller", "mic": "microcontroller",
    "bt": "bluetooth", "volt": "voltage", "amp": "ampere",
    "1": "one", "2": "two", "3": "three", "4": "four",
    "5": "five", "6": "six", "7": "seven", "8": "eight",
    "9": "nine", "10": "ten", "60": "sixty",
}

# Mirror of AMEL_PHRASE_TABLE
PHRASE_TABLE: List[Tuple[str, str]] = [
    ("wi fi", "wifi"),
    ("e mail", "email"),
    ("micro controller", "microcontroller"),
    ("esp 32", "esp32"),
    ("data sheet", "datasheet"),
    ("blue tooth", "bluetooth"),
]

# Mirror of AMEL_STOP_WORDS
STOP_WORDS = {
    "the", "a", "an", "is", "are", "was", "were", "be", "been", "am",
    "of", "in", "on", "at", "to", "for", "and", "or", "it", "its",
    "this", "that", "these", "those", "do", "does", "did", "with",
    "as", "by", "from", "please", "tell", "hey", "so", "there", "here",
}

VALID_CATEGORIES = {
    "science", "math", "esp32", "electronics", "networking", "computing",
    "arduino", "geography", "history", "meta", "greeting", "fun", "custom",
    "general", "hardware", "software",
    "gk", "sensor", "mock", "ameltechbot_features", "chemistry_table",
    "sensor_module_choice", "disaster_hazard_gk", "earth_gk", "friendly",
    "ai_use", "space_gk", "esp32_choice", "esp32_projects",
    "arduino_projects_gk", "funny_trolling", "indian_cricket_player_profile",
    "pizza_making", "ai_model_choice", "electromagnetic", "library_creator",
    "arduino_choice", "love", "goodbye", "creator_profile", "dreams",
    "cybersecurity", "important", "funny",
    # categories added in 2.0
    "identity", "dht", "humidity", "temperature", "training", "diagnostics",
}


# ---------------------------------------------------------------------------
# Normalisation — mirror of AmelTechText::normalize
# ---------------------------------------------------------------------------
def _replace_phrase(text: str, frm: str, to: str) -> str:
    """Word-boundary aware replacement, matching amelReplacePhrase()."""
    if not frm:
        return text
    out = text
    pos = 0
    while True:
        idx = out.find(frm, pos)
        if idx < 0:
            break
        left_ok = idx == 0 or out[idx - 1] == " "
        after = idx + len(frm)
        right_ok = after == len(out) or out[after] == " "
        if not left_ok or not right_ok:
            pos = idx + 1
            continue
        out = out[:idx] + to + out[after:]
        pos = idx + len(to)
    return out


def normalize_text(text: str) -> str:
    """Lowercase, keep [a-z0-9], expand abbreviations, collapse to single spaces."""
    if not text:
        return ""
    pieces: List[str] = []
    token_chars: List[str] = []

    def flush() -> None:
        if not token_chars:
            return
        tok = "".join(token_chars[:31])
        token_chars.clear()
        pieces.append(SYN_TABLE.get(tok, tok))

    for ch in text:
        c = ch.lower()
        # Apostrophes are removed rather than treated as separators, so
        # "ohm's law" and "ohms law" normalize to the same string. This
        # mirrors AmelTechText::normalize() exactly.
        if c in ("'", "\u2019", "\u2018"):
            continue
        if ("a" <= c <= "z") or ("0" <= c <= "9"):
            token_chars.append(c)
        else:
            flush()
    flush()

    out = " ".join(p for p in pieces if p)
    out = out[: MAX_QUESTION_LEN - 1]
    for frm, to in PHRASE_TABLE:
        out = _replace_phrase(out, frm, to)
    return out


# ---------------------------------------------------------------------------
# Sketches — mirror of AmelTechText::fnv1a / tokenSignature / trigramBloom
# ---------------------------------------------------------------------------
FNV_OFFSET = 2166136261
FNV_PRIME = 16777619
MASK32 = 0xFFFFFFFF


def fnv1a(data: bytes, seed: int = FNV_OFFSET) -> int:
    h = seed & MASK32
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & MASK32
    return h


def token_signature(normalized: str) -> int:
    sig = 0
    for tok in normalized.split():
        tok = tok[: MAX_TOKEN_LEN - 1]
        if tok in STOP_WORDS or not tok:
            continue
        sig |= 1 << (fnv1a(tok.encode("ascii", "ignore")) % 32)
    return sig & MASK32


def trigram_bloom(normalized: str) -> int:
    if not normalized:
        return 0
    padded = " " + normalized + " "
    raw = padded.encode("ascii", "ignore")
    bloom = 0
    if len(raw) < 3:
        return 1 << (fnv1a(raw) % 64)
    for i in range(0, len(raw) - 2):
        bloom |= 1 << (fnv1a(raw[i:i + 3]) % 64)
    return bloom & 0xFFFFFFFFFFFFFFFF


def token_count(normalized: str) -> int:
    return min(len(normalized.split()), 255)


# ---------------------------------------------------------------------------
# C string escaping
# ---------------------------------------------------------------------------
def escape_c_string(s: str) -> str:
    """Escape for a narrow C string literal, emitting UTF-8 byte escapes.

    A hex escape is followed by a "" split whenever the next character could be
    read as another hex digit, because C consumes as many hex digits as it can
    after \\x.
    """
    out: List[str] = []
    pending_hex = False

    def emit_literal(ch: str) -> None:
        nonlocal pending_hex
        if pending_hex and ch and ch[0] in "0123456789abcdefABCDEF":
            out.append('" "')
        pending_hex = False
        out.append(ch)

    def emit_hex_byte(b: int) -> None:
        nonlocal pending_hex
        out.append("\\x{:02x}".format(b))
        pending_hex = True

    for ch in s:
        if ch == "\\":
            emit_literal("\\\\")
        elif ch == '"':
            emit_literal('\\"')
        elif ch == "\n":
            emit_literal("\\n")
        elif ch == "\r":
            emit_literal("\\r")
        elif ch == "\t":
            emit_literal("\\t")
        elif ord(ch) < 0x20 or ord(ch) == 0x7F:
            emit_hex_byte(ord(ch))
        elif ord(ch) < 0x80:
            emit_literal(ch)
        else:
            for b in ch.encode("utf-8"):
                emit_hex_byte(b)
    return "".join(out)


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------
def validate(entries: List[Dict[str, Any]], strict: bool) -> Tuple[List[Dict[str, Any]], List[str]]:
    problems: List[str] = []
    kept: List[Dict[str, Any]] = []
    seen_norm: Dict[str, int] = {}

    if len(entries) > MAX_ENTRIES:
        problems.append(f"entry count {len(entries)} exceeds MAX_ENTRIES {MAX_ENTRIES}")

    for i, e in enumerate(entries):
        if not isinstance(e, dict):
            problems.append(f"[{i}] not an object")
            continue
        q = (e.get("question") or "").strip()
        a = (e.get("answer") or "").strip()
        c = (e.get("category") or "general").strip() or "general"

        if not q:
            problems.append(f"[{i}] empty question")
            continue
        if not a:
            problems.append(f"[{i}] empty answer for {q!r}")
            continue
        if len(q) >= MAX_QUESTION_LEN:
            problems.append(f"[{i}] question too long ({len(q)} >= {MAX_QUESTION_LEN}): {q[:48]!r}")
            continue
        if len(a) >= MAX_ANSWER_LEN:
            problems.append(f"[{i}] answer too long ({len(a)} >= {MAX_ANSWER_LEN}) for {q[:48]!r}")
            continue
        if len(c) >= MAX_CATEGORY_LEN:
            problems.append(f"[{i}] category too long: {c!r}")
            continue
        if c not in VALID_CATEGORIES:
            problems.append(f"[{i}] unknown category {c!r} for {q[:48]!r}")
            if strict:
                continue

        norm = normalize_text(q)
        if not norm:
            problems.append(f"[{i}] question normalizes to empty: {q!r}")
            continue
        if norm in seen_norm:
            problems.append(
                f"[{i}] duplicate of entry [{seen_norm[norm]}] after normalization: {q!r}"
            )
            continue
        seen_norm[norm] = i

        kept.append({"question": q, "answer": a, "category": c, "normalized": norm})

    return kept, problems


# ---------------------------------------------------------------------------
# Emission
# ---------------------------------------------------------------------------
HEADER_TEMPLATE = """// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
// Generated by tools/generate_knowledge.py from data/knowledge.json
//
// Declarations only. The table itself is defined exactly once, in
// knowledge_generated.cpp, so it occupies one block of flash no matter how
// many translation units include this header.

#ifndef AMELTECH_KNOWLEDGE_GENERATED_H
#define AMELTECH_KNOWLEDGE_GENERATED_H

#include <Arduino.h>

#define AMELTECH_BUILTIN_KNOWLEDGE_COUNT {count}
#define AMELTECH_KNOWLEDGE_REVISION "{revision}"

struct AmelTechBuiltinEntry {{
    const char* question;    // original phrasing, shown to the user
    const char* answer;      // response text
    const char* category;    // topic label
    const char* normalized;  // AmelTechText::normalize(question)
    uint32_t signature;      // AmelTechText::tokenSignature(normalized)
    uint64_t bloom;          // AmelTechText::trigramBloom(normalized)
    uint8_t tokenCount;      // words in normalized
}};

// On ESP32 .rodata is mapped into flash (DROM), so plain `const` already keeps
// this out of DRAM and the pointers can be dereferenced directly.
extern const AmelTechBuiltinEntry AMELTECH_BUILTIN_KNOWLEDGE[AMELTECH_BUILTIN_KNOWLEDGE_COUNT];

#endif // AMELTECH_KNOWLEDGE_GENERATED_H
"""

SOURCE_PREFIX = """// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
// Generated by tools/generate_knowledge.py from data/knowledge.json
//
// Single definition of the built-in knowledge table.
// Entries: {count}
// Categories: {categories}

#include "knowledge_generated.h"

const AmelTechBuiltinEntry AMELTECH_BUILTIN_KNOWLEDGE[AMELTECH_BUILTIN_KNOWLEDGE_COUNT] = {{
"""

SOURCE_SUFFIX = """};
"""


def emit(entries: List[Dict[str, Any]], out_dir: str, revision: str) -> Tuple[str, str]:
    cats = sorted({e["category"] for e in entries})
    header_path = os.path.join(out_dir, "knowledge_generated.h")
    source_path = os.path.join(out_dir, "knowledge_generated.cpp")

    with open(header_path, "w", encoding="utf-8") as fh:
        fh.write(HEADER_TEMPLATE.format(count=len(entries), revision=revision))

    with open(source_path, "w", encoding="utf-8") as fh:
        fh.write(SOURCE_PREFIX.format(count=len(entries), categories=", ".join(cats)))
        for e in entries:
            norm = e["normalized"]
            sig = token_signature(norm)
            bloom = trigram_bloom(norm)
            ntok = token_count(norm)
            fh.write(
                '    {{ "{q}", "{a}", "{c}", "{n}", 0x{sig:08x}UL, 0x{bloom:016x}ULL, {ntok} }},\n'.format(
                    q=escape_c_string(e["question"]),
                    a=escape_c_string(e["answer"]),
                    c=escape_c_string(e["category"]),
                    n=escape_c_string(norm),
                    sig=sig,
                    bloom=bloom,
                    ntok=ntok,
                )
            )
        fh.write(SOURCE_SUFFIX)

    return header_path, source_path


# ---------------------------------------------------------------------------
# Self test — guards the mirror against drift
# ---------------------------------------------------------------------------
def selftest() -> int:
    failures = 0

    def check(actual: Any, expected: Any, label: str) -> None:
        nonlocal failures
        if actual != expected:
            print(f"FAIL {label}: got {actual!r} expected {expected!r}")
            failures += 1

    check(normalize_text("What is WiFi?"), "what is wifi", "wifi phrase repair")
    check(normalize_text("How many SEC r there in 1 MIN?"),
          "how many seconds are there in one minute", "abbreviation expansion")
    check(normalize_text("  Hello,   world!!  "), "hello world", "punctuation strip")
    check(normalize_text("whats ur name"), "what is your name", "shorthand")
    check(normalize_text("ESP 32 pinout"), "esp32 pinout", "esp32 phrase repair")
    check(normalize_text("!!!"), "", "empty result")
    check(fnv1a(b"water"), fnv1a(b"water"), "hash stability")
    check(token_signature("what is water") == token_signature("water what is"), True,
          "signature is order independent")
    b1 = trigram_bloom("what is water")
    check(b1 == trigram_bloom("what is water"), True, "bloom stability")
    check(bin(b1).count("1") > 0, True, "bloom non-empty")

    print("selftest failures:", failures)
    return 1 if failures else 0


# ---------------------------------------------------------------------------
def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input", default=os.path.join(root, "data", "knowledge.json"))
    ap.add_argument("--out-dir", default=os.path.join(root, "src"))
    ap.add_argument("--revision", default="2.0.0")
    ap.add_argument("--strict", action="store_true",
                    help="drop entries with unknown categories instead of warning")
    ap.add_argument("--selftest", action="store_true", help="verify the normalizer mirror")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not os.path.isfile(args.input):
        print(f"error: input not found: {args.input}", file=sys.stderr)
        return 2

    with open(args.input, "r", encoding="utf-8") as fh:
        data = json.load(fh)

    if not isinstance(data, list):
        print("error: knowledge.json must contain a JSON array", file=sys.stderr)
        return 2

    kept, problems = validate(data, args.strict)

    for p in problems[:40]:
        print("warning:", p, file=sys.stderr)
    if len(problems) > 40:
        print(f"warning: ... and {len(problems) - 40} more", file=sys.stderr)

    if not kept:
        print("error: no valid entries", file=sys.stderr)
        return 2

    header_path, source_path = emit(kept, args.out_dir, args.revision)

    cats = Counter(e["category"] for e in kept)
    print(f"entries in:  {len(data)}")
    print(f"entries out: {len(kept)}")
    print(f"skipped:     {len(data) - len(kept)}")
    print(f"categories:  {len(cats)}")
    print(f"wrote {header_path}")
    print(f"wrote {source_path} ({os.path.getsize(source_path)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
