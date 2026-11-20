#!/usr/bin/env python3
"""source.no_raw_sheets — NO RAW STYLESHEET OUTSIDE src/ui/style/ (CODING_RULES §2; restored by STUDIO-HYGIENE-1
stage 4 from archive/tests/theme, where TEST-CUT-1 had put it).

The live walk (theme.sheets / app.styleSheets) only sees widgets that exist during its tour; this reads the
SOURCE. Three rules, NO allowlist:
  1. every `setStyleSheet(` call outside src/ui/style/ passes ONLY GETTERS (StyleSheet::, ThemeManager::,
     ThemeRoles::): its argument, comments and getter calls stripped, may hold nothing but a condition and the
     `?` `:` choosing between two getters (`open ? StyleSheet::A() : StyleSheet::B()`). A literal, a named
     constant, a cast or a concatenation is a raw sheet (the audit: a brace-less sheet in a constant, a sheet
     built by `+`, a sheet read from a .qss file all passed the literal-only rule);
  2. no .ui form carries a `styleSheet` property;
  3. NEW: no CSS-SHAPED string literal (a selector, a brace, then a property: `QLabel { color: ... }`) in a
     .cpp/.h outside src/ui/style/ — the sheet passed as a NAMED CONSTANT, which rule 1 cannot see (the chat
     window's `setStyleSheet(QString::fromLatin1(kWindowStyle))` passed every literal check while its sheet sat
     in the same file). Raw string literals R"(...)" are read too. An inline `style="color: ..."` attribute in
     rich-text HTML has no braces and is not a sheet.
Exit 1 with the offending lines. $1 = the repo root."""
import os, re, sys

root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", ".."))
STYLE = os.path.join("src", "ui", "style")
call = re.compile(r"setStyleSheet\s*\(")
literal = re.compile(r'"((?:[^"\\]|\\.)*)"')
getter = re.compile(r"(?:StyleSheet|ThemeManager|ThemeRoles)\s*::\s*\w+\s*\(")
# A C++ string literal: a raw one R"delim( ... )delim", or an ordinary one.
any_literal = re.compile(r'R"([^(\s]*)\((.*?)\)\1"|"((?:[^"\\\n]|\\.)*)"', re.S)
# CSS: a selector, an open brace, a property and a colon - the shape of a sheet, not of JSON (whose keys are
# quoted) or HTML (whose style attribute has no braces).
css = re.compile(r"[A-Za-z#*][\w#.:\-\s,>*]*\{\s*(?:[^{}]*;\s*)?-?(?:background|color|border|padding|margin|font|"
                 r"selection-background-color|outline|spacing|min-width|min-height)[\w-]*\s*:", re.S)
bad = []
# What a getters-only argument leaves once its getter calls are stripped (whitespace removed): nothing, or a
# condition — an identifier, a member read or a no-argument call, optionally negated — then `?`, then `:`.
GETTERS_ONLY = re.compile(r"(?:(?:!?[A-Za-z_]\w*(?:(?:\.|->)\w+)*(?:\(\))?)?\?)?:?")


def strip_getters(arg):
    out, i = [], 0
    while True:
        m = getter.search(arg, i)
        if not m:
            out.append(arg[i:])
            return "".join(out)
        out.append(arg[i:m.start()])
        depth, j = 1, m.end()
        while j < len(arg) and depth:
            depth += {"(": 1, ")": -1}.get(arg[j], 0)
            j += 1
        i = j


def strip_comments(text):
    """Comments blanked (newlines kept, so line numbers hold); string literals left alone."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(c if c == "\n" else " " for c in text[i:j])); i = j
        elif text.startswith('R"', i):
            m = any_literal.match(text, i)
            j = m.end() if m else i + 2
            out.append(text[i:j]); i = j
        elif text[i] == '"':
            m = any_literal.match(text, i)
            j = m.end() if m else i + 1
            out.append(text[i:j]); i = j
        elif text[i] == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1]); i = j + 1
        else:
            out.append(text[i]); i += 1
    return "".join(out)


for base, dirs, files in os.walk(os.path.join(root, "src")):
    rel_base = os.path.relpath(base, root)
    if rel_base == STYLE or rel_base.startswith(STYLE + os.sep):
        continue
    for f in sorted(files):
        path = os.path.join(base, f)
        rel = os.path.relpath(path, root)
        if f.endswith(".ui"):
            if 'name="styleSheet"' in open(path, encoding="utf-8", errors="replace").read():
                bad.append(f"{rel}: .ui form carries a styleSheet property")
            continue
        if not f.endswith((".cpp", ".h")):
            continue
        text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        for m in call.finditer(text):                                   # rule 1
            depth, i = 1, m.end()
            while i < len(text) and depth:
                depth += {"(": 1, ")": -1}.get(text[i], 0)
                i += 1
            arg = text[m.end():i - 1]
            residue = "".join(strip_getters(arg).split())
            if not GETTERS_ONLY.fullmatch(residue):
                line = text.count("\n", 0, m.start()) + 1
                bad.append(f"{rel}:{line}: raw sheet: setStyleSheet({' '.join(arg.split())[:80]})")
        for m in any_literal.finditer(text):                            # rule 3
            body = m.group(2) if m.group(2) is not None else m.group(3)
            if body and css.search(body):
                line = text.count("\n", 0, m.start()) + 1
                bad.append(f"{rel}:{line}: a stylesheet in a string literal: {' '.join(body.split())[:80]}")

if bad:
    print("source.no_raw_sheets: FAIL — %d raw sheet(s) outside src/ui/style/:" % len(bad))
    for b in bad:
        print("    " + b)
    print("    Move the sheet into the theme (ThemeManager or StyleSheet, src/ui/style/) and call its getter.")
    sys.exit(1)
print("source.no_raw_sheets: ok — no raw stylesheet outside src/ui/style/")
