#!/usr/bin/env python3
"""
generate_knowledge.py

Validates data/knowledge.json and generates src/knowledge_generated.h
for flash-friendly PROGMEM storage on ESP32.

Usage:
  python3 tools/generate_knowledge.py
  python3 tools/generate_knowledge.py --input data/knowledge.json --output src/knowledge_generated.h
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import defaultdict
from typing import Any, Dict, List, Set, Tuple

# Limits (keep aligned with library constants)
MAX_QUESTION_LEN = 128
MAX_ANSWER_LEN = 384
MAX_CATEGORY_LEN = 32
# Raised from the original 512 to accommodate the expanded training data set
# (~2000 entries, ~300KB of flash strings — comfortably within typical ESP32
# flash budgets of several MB). This is a sanity ceiling, not a hard hardware
# limit; raise further if the knowledge base keeps growing.
MAX_ENTRIES = 4096

VALID_CATEGORIES = {
    "science", "math", "esp32", "electronics", "networking", "computing",
    "arduino", "geography", "history", "meta", "greeting", "fun", "custom",
    "general", "hardware", "software",
    # Extended categories introduced by the expanded training data set.
    "gk", "sensor", "mock", "ameltechbot_features", "chemistry_table",
    "sensor_module_choice", "disaster_hazard_gk", "earth_gk", "friendly",
    "ai_use", "space_gk", "esp32_choice", "esp32_projects",
    "arduino_projects_gk", "funny_trolling", "indian_cricket_player_profile",
    "pizza_making", "ai_model_choice", "electromagnetic", "library_creator",
    "arduino_choice", "love", "goodbye", "creator_profile", "dreams",
    "cybersecurity", "important",
}

ABBREV_MAP = {
    "sec": "seconds",
    "secs": "seconds",
    "min": "minute",
    "mins": "minutes",
    "hr": "hour",
    "hrs": "hours",
    "r": "are",
    "u": "you",
    "ur": "your",
    "whats": "what is",
    "whats": "what is",
    "wht": "what",
    "hw": "how",
    "pls": "please",
    "thx": "thanks",
    "temp": "temperature",
    "freq": "frequency",
    "mem": "memory",
    "cfg": "configuration",
}


def normalize_text(text: str) -> str:
    """Normalize for duplicate detection: lowercase, strip punctuation, collapse whitespace, expand common abbrevs."""
    t = text.lower().strip()
    t = re.sub(r"[^\w\s]", " ", t)
    tokens = t.split()
    expanded = []
    for tok in tokens:
        expanded.append(ABBREV_MAP.get(tok, tok))
    t = " ".join(expanded)
    t = re.sub(r"\s+", " ", t).strip()
    return t


def escape_c_string(s: str) -> str:
    """Escape a string for inclusion in a C string literal.

    Non-ASCII characters are encoded as UTF-8 and each resulting byte is
    emitted as its own \\xNN escape (valid range 0x00-0xFF). Emitting the
    raw codepoint (e.g. \\x2014 for an em dash) is invalid for a narrow
    char string literal — codepoints above 0xFF silently truncate/misbehave
    across compilers. Splitting into UTF-8 bytes keeps the on-device string
    correct for any UTF-8-aware display (Serial monitor, most terminals).

    A hex escape is followed by a "" split whenever the next source
    character could itself be interpreted as a hex digit, since C consumes
    as many hex digits as possible after \\x (e.g. "\\xc3" followed by "a"
    would otherwise be parsed as one invalid escape "\\xc3a").
    """
    out = []
    pending_hex = False  # true if the last emitted char was part of a \xNN escape

    def emit_literal(ch: str):
        nonlocal pending_hex
        if pending_hex and ch in "0123456789abcdefABCDEF":
            out.append('" "')
        pending_hex = False
        out.append(ch)

    def emit_hex_byte(b: int):
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
        elif 32 <= ord(ch) <= 126:
            emit_literal(ch)
        else:
            # Non-ASCII or other control character: encode as UTF-8 bytes.
            for b in ch.encode("utf-8"):
                emit_hex_byte(b)
    return "".join(out)


def validate_entry(entry: Dict[str, Any], index: int) -> List[str]:
    errors = []
    if not isinstance(entry, dict):
        return [f"Entry {index}: not an object"]
    for field in ("question", "answer", "category"):
        if field not in entry:
            errors.append(f"Entry {index}: missing required field '{field}'")
    if errors:
        return errors

    q = entry["question"]
    a = entry["answer"]
    c = entry["category"]

    if not isinstance(q, str) or not q.strip():
        errors.append(f"Entry {index}: empty or invalid question")
    elif len(q) > MAX_QUESTION_LEN:
        errors.append(f"Entry {index}: question exceeds {MAX_QUESTION_LEN} chars")

    if not isinstance(a, str) or not a.strip():
        errors.append(f"Entry {index}: empty or invalid answer")
    elif len(a) > MAX_ANSWER_LEN:
        errors.append(f"Entry {index}: answer exceeds {MAX_ANSWER_LEN} chars")

    if not isinstance(c, str) or not c.strip():
        errors.append(f"Entry {index}: empty or invalid category")
    elif len(c) > MAX_CATEGORY_LEN:
        errors.append(f"Entry {index}: category exceeds {MAX_CATEGORY_LEN} chars")
    elif c.lower() not in VALID_CATEGORIES:
        # warn but allow custom categories that are reasonable
        if not re.match(r"^[a-z0-9_\-]{1,32}$", c.lower()):
            errors.append(f"Entry {index}: invalid category format '{c}'")

    return errors


def load_and_validate(path: str) -> Tuple[List[Dict[str, str]], List[str]]:
    errors: List[str] = []
    with open(path, "r", encoding="utf-8") as f:
        try:
            data = json.load(f)
        except json.JSONDecodeError as e:
            return [], [f"JSON parse error: {e}"]

    if not isinstance(data, list):
        return [], ["Root JSON must be an array"]

    if len(data) > MAX_ENTRIES:
        errors.append(f"Too many entries: {len(data)} > {MAX_ENTRIES}")

    entries: List[Dict[str, str]] = []
    seen_raw: Set[str] = set()
    seen_norm: Set[str] = set()

    for i, item in enumerate(data):
        errs = validate_entry(item, i)
        errors.extend(errs)
        if errs:
            continue

        q = item["question"].strip()
        a = item["answer"].strip()
        c = item["category"].strip().lower()

        q_lower = q.lower()
        if q_lower in seen_raw:
            errors.append(f"Entry {i}: duplicate question (exact): '{q}'")
            continue
        seen_raw.add(q_lower)

        q_norm = normalize_text(q)
        if q_norm in seen_norm:
            errors.append(f"Entry {i}: duplicate question (normalized): '{q}' -> '{q_norm}'")
            continue
        seen_norm.add(q_norm)

        entries.append({
            "question": q,
            "answer": a,
            "category": c,
            "normalized": q_norm,
        })

    return entries, errors


def generate_header(entries: List[Dict[str, str]], output_path: str) -> None:
    lines: List[str] = []
    lines.append("// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY")
    lines.append("// Generated by tools/generate_knowledge.py")
    lines.append("// Source: data/knowledge.json")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append("#ifndef AMELTECH_KNOWLEDGE_GENERATED_H")
    lines.append("#define AMELTECH_KNOWLEDGE_GENERATED_H")
    lines.append("")
    lines.append(f"#define AMELTECH_BUILTIN_KNOWLEDGE_COUNT {len(entries)}")
    lines.append("")
    lines.append("struct AmelTechBuiltinEntry {")
    lines.append("    const char* question;")
    lines.append("    const char* answer;")
    lines.append("    const char* category;")
    lines.append("    const char* normalized;")
    lines.append("};")
    lines.append("")
    lines.append("// Stored in flash (PROGMEM) where supported")
    lines.append("#if defined(PROGMEM)")
    lines.append("static const AmelTechBuiltinEntry AMELTECH_BUILTIN_KNOWLEDGE[] PROGMEM = {")
    lines.append("#else")
    lines.append("static const AmelTechBuiltinEntry AMELTECH_BUILTIN_KNOWLEDGE[] = {")
    lines.append("#endif")

    for e in entries:
        q = escape_c_string(e["question"])
        a = escape_c_string(e["answer"])
        c = escape_c_string(e["category"])
        n = escape_c_string(e["normalized"])
        lines.append(f'    {{ "{q}", "{a}", "{c}", "{n}" }},')

    lines.append("};")
    lines.append("")
    lines.append("#endif // AMELTECH_KNOWLEDGE_GENERATED_H")
    lines.append("")

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate knowledge_generated.h from knowledge.json")
    parser.add_argument("--input", default="data/knowledge.json", help="Path to knowledge.json")
    parser.add_argument("--output", default="src/knowledge_generated.h", help="Output header path")
    parser.add_argument("--root", default=None, help="Project root (defaults to parent of tools/)")
    args = parser.parse_args()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    root = args.root or os.path.dirname(script_dir)

    input_path = args.input if os.path.isabs(args.input) else os.path.join(root, args.input)
    output_path = args.output if os.path.isabs(args.output) else os.path.join(root, args.output)

    if not os.path.isfile(input_path):
        print(f"ERROR: Input file not found: {input_path}", file=sys.stderr)
        return 1

    entries, errors = load_and_validate(input_path)

    if errors:
        print("Validation errors:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        if not entries:
            return 1
        print("WARNING: Some entries were rejected; continuing with valid entries.", file=sys.stderr)

    generate_header(entries, output_path)
    print(f"Generated {output_path}")
    print(f"  Entries: {len(entries)}")
    print(f"  Max question length: {MAX_QUESTION_LEN}")
    print(f"  Max answer length: {MAX_ANSWER_LEN}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
