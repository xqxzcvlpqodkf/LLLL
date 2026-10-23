#!/usr/bin/env python3
"""Sanity checks for the Neon Dash source tree.

This does not compile the mod (that needs the Geode SDK), but it catches the
mistakes that are easiest to make and hardest to see: a setting key that is
referenced in C++ but missing from mod.json (or misspelled), and unbalanced
braces. Run it before pushing:

    python3 scripts/check-settings.py
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"


def fail(message: str) -> None:
    print(f"  FAIL  {message}")


def main() -> int:
    errors = 0

    # 1. mod.json parses and has the required keys.
    mod_json = ROOT / "mod.json"
    try:
        data = json.loads(mod_json.read_text())
    except json.JSONDecodeError as exc:
        fail(f"mod.json is not valid JSON: {exc}")
        return 1

    for key in ("geode", "id", "name", "version", "developer"):
        if key not in data:
            fail(f"mod.json is missing required key '{key}'")
            errors += 1

    if not re.fullmatch(r"[a-z0-9.\-_]+", data.get("id", "")):
        fail(f"mod id '{data.get('id')}' may only contain a-z, 0-9, '.', '_' and '-'")
        errors += 1

    settings = data.get("settings", {})
    declared = {k for k, v in settings.items() if v.get("type") != "title"}

    # 2. Every setting referenced in C++ exists in mod.json.
    used: set[str] = set()
    access_re = re.compile(
        r'(?:getSettingValue<[^>]+>|setSettingValue<[^>]+>|listenForSettingChanges<[^>]+>'
        r'|bind<[^>]+>|listenForKeybindSettingPresses)'
        r'\(\s*"([a-z0-9\-]+)"'
    )
    sources = sorted(SRC.glob("*.cpp")) + sorted(SRC.glob("*.hpp"))
    all_source = ""
    for path in sources:
        text = path.read_text()
        all_source += text
        for match in access_re.finditer(text):
            used.add(match.group(1))

    # Keys are also referenced from the constexpr tables in QuickMenu.cpp, so
    # fall back to "the literal appears somewhere in the source".
    used |= {k for k in declared if f'"{k}"' in all_source}

    for key in sorted(used - declared):
        fail(f"'{key}' is used in C++ but is not a setting in mod.json")
        errors += 1

    # 3. Settings that are never read anywhere are usually leftovers.
    for key in sorted(declared - used):
        print(f"  note  '{key}' is declared in mod.json but never read in C++")

    # 4. Brace / paren balance per file, ignoring strings and comments.
    for path in sorted(SRC.iterdir()):
        if path.suffix not in (".cpp", ".hpp"):
            continue
        text = path.read_text()
        text = re.sub(r'//[^\n]*', "", text)
        text = re.sub(r'/\*.*?\*/', "", text, flags=re.S)
        text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
        for open_char, close_char in (("{", "}"), ("(", ")")):
            balance = text.count(open_char) - text.count(close_char)
            if balance != 0:
                fail(f"{path.name}: {balance:+d} unbalanced '{open_char}'")
                errors += 1

    if errors:
        print(f"\n{errors} problem(s) found.")
        return 1

    print(f"OK - {len(declared)} settings declared, {len(used)} referenced, all files balanced.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
