#!/usr/bin/env python3
"""
generate_knowledge.py

Validates data/knowledge.json and generates src/knowledge_generated.h,
a flash/PROGMEM-friendly C++ header containing the built-in knowledge base
for the AmelTechBot ESP32 Arduino library.

Workflow:
    data/knowledge.json -> validation -> generate_knowledge.py -> knowledge_generated.h

Usage:
    python3 tools/generate_knowledge.py \
        --input data/knowledge.json \
        --output src/knowledge_generated.h

Exit codes:
    0  success
    1  validation error (details printed to stderr)
    2  I/O error
"""

import argparse
import json
import re
import sys
from datetime import datetime, timezone

# ---------------------------------------------------------------------------
# Configuration / limits
# ---------------------------------------------------------------------------

MAX_QUESTION_LEN = 110      # must match AMELTECH_MAX_QUESTION_LEN in the C++ headers
MAX_ANSWER_LEN = 360       # must match AMELTECH_MAX_ANSWER_LEN in the C++ headers
MAX_CATEGORY_LEN = 47
MAX_ENTRIES = 2112

VALID_CATEGORIES = {
    "ai_model_choice",
    "ameltechbot_features",
    "animals_gk",
    "arduino_choice",
    "arduino_projects_gk",
    "biology_gk",
    "bollywood_actor",
    "businessman_profile",
    "chemistry_table",
    "clash_of_clans",
    "creator_profile",
    "custom",
    "delhi_gk",
    "digital_analog_gk",
    "disaster_hazard_gk",
    "dreams",
    "earth_gk",
    "electrical_gk",
    "esp32",
    "esp32_choice",
    "esp32_projects",
    "favourite_gk",
    "football_fan",
    "football_player_celebrity_profile",
    "football_player_profile",
    "friendly_talk",
    "funny_trolling",
    "general",
    "goodbye",
    "greeting",
    "gta",
    "head_pain_care_types_gk",
    "healthcare_care_gk",
    "healthcare_gk",
    "history_gk",
    "hollywood_actor",
    "human_gk",
    "india_gk",
    "indian_businessman_profile",
    "indian_cricket_player_profile",
    "indian_freedom_history_gk",
    "indian_history_gk",
    "indian_politician_profile",
    "international_politician_profile",
    "kerala_actor",
    "kerala_gk",
    "kerala_politician_profile",
    "library_creator",
    "library_use",
    "malayalam_actor",
    "math",
    "pizza_making",
    "plants_gk",
    "relationship_care_gk",
    "science",
    "scientist_discoveries_gk",
    "scientist_gk",
    "sensor_module_choice",
    "si_units_gk",
    "space_gk",
    "tamil_actor",
    "tea_making_gk",
    "temple_run",
    "video_game",
    "war_gk",
    "world_gk",
}


HEADER_GUARD = "AMELTECH_KNOWLEDGE_GENERATED_H"


class ValidationError(Exception):
    pass


def normalize(text: str) -> str:
    """Lowercase, trim, and collapse whitespace/punctuation the same way the
    C++ runtime normalizer will, so duplicate detection matches runtime
    matching behavior."""
    text = text.strip().lower()
    text = re.sub(r"[^\w\s]", "", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def escape_cpp_string(s: str) -> str:
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 32 or ord(ch) > 126:
            out.append("\\x{:02x}".format(ord(ch)))
        else:
            out.append(ch)
    return "".join(out)


def load_dataset(path: str):
    try:
        with open(path, "r", encoding="utf-8") as f:
            raw = f.read()
    except OSError as e:
        print(f"ERROR: could not read {path}: {e}", file=sys.stderr)
        sys.exit(2)

    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        print(f"ERROR: invalid JSON in {path}: {e}", file=sys.stderr)
        sys.exit(1)

    if not isinstance(data, list):
        print("ERROR: knowledge.json must contain a JSON array at the top level", file=sys.stderr)
        sys.exit(1)

    return data


def validate_dataset(data):
    errors = []
    seen_exact = {}
    seen_normalized = {}

    if len(data) > MAX_ENTRIES:
        errors.append(
            f"Dataset has {len(data)} entries, exceeding MAX_ENTRIES={MAX_ENTRIES}"
        )

    for idx, entry in enumerate(data):
        loc = f"entry[{idx}]"

        if not isinstance(entry, dict):
            errors.append(f"{loc}: not a JSON object")
            continue

        # Required fields
        for field in ("question", "answer", "category"):
            if field not in entry:
                errors.append(f"{loc}: missing required field '{field}'")

        if errors and any(e.startswith(loc) for e in errors[-3:]):
            # If required fields are missing we can't safely continue checks on this entry
            if "question" not in entry or "answer" not in entry or "category" not in entry:
                continue

        question = entry.get("question", "")
        answer = entry.get("answer", "")
        category = entry.get("category", "")

        if not isinstance(question, str) or not isinstance(answer, str) or not isinstance(category, str):
            errors.append(f"{loc}: question/answer/category must be strings")
            continue

        # Reject empty
        if len(question.strip()) == 0:
            errors.append(f"{loc}: question is empty")
        if len(answer.strip()) == 0:
            errors.append(f"{loc}: answer is empty")

        # Length limits
        if len(question) > MAX_QUESTION_LEN:
            errors.append(
                f"{loc}: question exceeds MAX_QUESTION_LEN={MAX_QUESTION_LEN} "
                f"({len(question)} chars): '{question[:40]}...'"
            )
        if len(answer) > MAX_ANSWER_LEN:
            errors.append(
                f"{loc}: answer exceeds MAX_ANSWER_LEN={MAX_ANSWER_LEN} "
                f"({len(answer)} chars): '{answer[:40]}...'"
            )
        if len(category) > MAX_CATEGORY_LEN:
            errors.append(f"{loc}: category exceeds MAX_CATEGORY_LEN={MAX_CATEGORY_LEN}")

        # Category validation
        if category not in VALID_CATEGORIES:
            errors.append(
                f"{loc}: invalid category '{category}'. Valid categories: "
                f"{sorted(VALID_CATEGORIES)}"
            )

        # Exact duplicate detection
        q_key = question.strip()
        if q_key in seen_exact:
            errors.append(
                f"{loc}: exact duplicate question of entry[{seen_exact[q_key]}]: '{q_key}'"
            )
        else:
            seen_exact[q_key] = idx

        # Normalized duplicate detection
        norm = normalize(question)
        if norm == "":
            errors.append(f"{loc}: question normalizes to an empty string")
        elif norm in seen_normalized:
            prev_idx = seen_normalized[norm]
            errors.append(
                f"{loc}: normalized duplicate of entry[{prev_idx}] "
                f"(normalized form: '{norm}')"
            )
        else:
            seen_normalized[norm] = idx

    if errors:
        raise ValidationError("\n".join(errors))

    return True


def build_header(data, output_path: str) -> str:
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    count = len(data)

    lines = []
    lines.append("// =============================================================")
    lines.append("// AUTO-GENERATED FILE. DO NOT EDIT BY HAND.")
    lines.append("//")
    lines.append("// Generated by tools/generate_knowledge.py from data/knowledge.json")
    lines.append(f"// Generated at: {timestamp}")
    lines.append(f"// Entry count:  {count}")
    lines.append("//")
    lines.append("// To modify the built-in knowledge base, edit data/knowledge.json")
    lines.append("// and re-run:")
    lines.append("//   python3 tools/generate_knowledge.py")
    lines.append("// =============================================================")
    lines.append(f"#ifndef {HEADER_GUARD}")
    lines.append(f"#define {HEADER_GUARD}")
    lines.append("")
    lines.append("#include <Arduino.h>")
    lines.append("")
    lines.append("// Stored in flash (PROGMEM) on AVR-style targets; on ESP32, string")
    lines.append("// literals in flash-qualified sections are placed in flash/rodata")
    lines.append("// automatically by the toolchain, so PROGMEM macros are used for")
    lines.append("// portability but are effectively no-ops on ESP32.")
    lines.append("")
    lines.append("struct AmelTechKnowledgeEntryPROGMEM {")
    lines.append("    const char* question;")
    lines.append("    const char* answer;")
    lines.append("    const char* category;")
    lines.append("};")
    lines.append("")
    lines.append(f"#define AMELTECH_GENERATED_KNOWLEDGE_COUNT {count}")
    lines.append("")

    # Individual PROGMEM string literals (question/answer/category) so they
    # are each placed in flash and referenced by pointer, avoiding RAM copies.
    for idx, entry in enumerate(data):
        q = escape_cpp_string(entry["question"].strip())
        a = escape_cpp_string(entry["answer"].strip())
        c = escape_cpp_string(entry["category"].strip())
        lines.append(f'static const char AMELTECH_KQ_{idx}[] PROGMEM = "{q}";')
        lines.append(f'static const char AMELTECH_KA_{idx}[] PROGMEM = "{a}";')
        lines.append(f'static const char AMELTECH_KC_{idx}[] PROGMEM = "{c}";')
    lines.append("")

    lines.append(
        "static const AmelTechKnowledgeEntryPROGMEM "
        "AMELTECH_GENERATED_KNOWLEDGE[AMELTECH_GENERATED_KNOWLEDGE_COUNT] PROGMEM = {"
    )
    for idx in range(count):
        lines.append(
            f"    {{ AMELTECH_KQ_{idx}, AMELTECH_KA_{idx}, AMELTECH_KC_{idx} }},"
        )
    lines.append("};")
    lines.append("")
    lines.append(f"#endif // {HEADER_GUARD}")
    lines.append("")

    content = "\n".join(lines)

    try:
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(content)
    except OSError as e:
        print(f"ERROR: could not write {output_path}: {e}", file=sys.stderr)
        sys.exit(2)

    return content


def main():
    parser = argparse.ArgumentParser(description="Generate ESP32 flash-friendly knowledge header")
    parser.add_argument("--input", default="data/knowledge.json", help="Path to knowledge.json")
    parser.add_argument("--output", default="src/knowledge_generated.h", help="Path to output header")
    args = parser.parse_args()

    data = load_dataset(args.input)

    try:
        validate_dataset(data)
    except ValidationError as e:
        print("VALIDATION FAILED:", file=sys.stderr)
        print(str(e), file=sys.stderr)
        sys.exit(1)

    build_header(data, args.output)

    print(f"OK: validated {len(data)} entries")
    print(f"OK: generated {args.output}")


if __name__ == "__main__":
    main()
