#!/usr/bin/env python3
"""theme.no_raw_sheets — the STATIC half of the no-raw-stylesheet law (theme review SF-3).

The live walk (theme.sheets / app.styleSheets) can only see widgets that exist during its
tour; menus and dialogs built on demand never meet it. This lint reads the SOURCE instead:
  1. every `setStyleSheet(` call outside src/ui/style/ whose argument contains a non-empty
     string literal is a raw sheet (a getter call or a ternary between getters has none);
  2. no .ui form carries a `styleSheet` property.
The one allowlisted window is the Claude chat window (its design is its content, identical in
both themes — claudechatwindow.h documents it). Exit 1 with the offending lines."""
import os, re, sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
root = os.path.abspath(root)
ALLOW = {os.path.join("src", "ui", "windows", "claudechatwindow.cpp")}
call = re.compile(r"setStyleSheet\s*\(")
literal = re.compile(r'"((?:[^"\\]|\\.)*)"')
bad = []
getter = re.compile(r"(?:StyleSheet|ThemeManager|ThemeRoles)\s*::\s*\w+\s*\(")


def strip_getters(arg):
    out, i = [], 0
    while True:
        m = getter.search(arg, i)
        if not m:
            out.append(arg[i:]); return "".join(out)
        out.append(arg[i:m.start()])
        depth, j = 1, m.end()
        while j < len(arg) and depth:
            depth += {"(": 1, ")": -1}.get(arg[j], 0); j += 1
        i = j

for base, dirs, files in os.walk(os.path.join(root, "src")):
    rel_base = os.path.relpath(base, root)
    if rel_base.startswith(os.path.join("src", "ui", "style")): continue
    for f in files:
        path = os.path.join(base, f); rel = os.path.relpath(path, root)
        if f.endswith(".ui"):
            if 'name="styleSheet"' in open(path, encoding="utf-8", errors="replace").read():
                bad.append(f"{rel}: .ui form carries a styleSheet property")
            continue
        if not f.endswith((".cpp", ".h")) or rel in ALLOW: continue
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in call.finditer(text):
            # the argument, up to the matching close paren
            depth, i = 1, m.end()
            while i < len(text) and depth:
                depth += {"(": 1, ")": -1}.get(text[i], 0); i += 1
            arg = text[m.end():i - 1]
            line_start = text.rfind("\n", 0, m.start()) + 1
            if text[line_start:m.start()].lstrip().startswith("//"): continue
            # A getter call (StyleSheet::, ThemeManager::, ThemeRoles::) may take literal
            # ARGUMENTS (a colour, an object name): strip each balanced getter call first,
            # then any literal left over is a raw sheet.
            residue = strip_getters(arg)
            if any(lit.strip() for lit in literal.findall(residue)):
                line = text.count("\n", 0, m.start()) + 1
                bad.append(f"{rel}:{line}: raw sheet: setStyleSheet({arg.strip()[:80]})")
if bad:
    print("theme.no_raw_sheets: %d raw sheet(s) — move each into a StyleSheet:: getter (Classic) "
          "or a ThemeRoles/ThemeManager call:" % len(bad))
    for b in bad: print("  " + b)
    sys.exit(1)
print("theme.no_raw_sheets: OK — no raw setStyleSheet literal outside src/ui/style, no .ui styleSheet property")
