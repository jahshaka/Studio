#!/usr/bin/env bash
#
# source.shell_boundaries — THE SHELL STAYS A SHELL (D10-SHELL-MODULES; the
# 2026-09-30 architecture audit, area 5: S3, S11, S14).
#
# Three rules, each one a thing that grew back one call site at a time before
# it had a gate:
#
#   1. NO `shell/` INCLUDE UNDER `src/modules/` OR `src/scripting/`. A module or
#      a verb that needs the window gets it through ui/ishellview.h (the
#      interface the shell implements) — 13 ApiModule TUs and a module used to
#      compile against the whole of shell/mainwindow.h (S3, S14).
#   2. NO `setCurrentIndex(<digit>)` IN `src/shell/`. Pages are keyed by name
#      (shell/pagehost.h); a literal stack index is how five of the seven
#      spaces came to differ from their enum value (S11).
#   3. `src/shell/mainwindow.cpp` INCLUDES NO MODULE HEADER BY NAME. The shell
#      drives modules through StudioModule's hooks (shell/modulehub.h) and
#      builds them from modules/moduleregistry.cpp; `modules/studiomodule.h`
#      (the contract) and the registry are the only headers under src/modules
#      it may name, and the Player MODULE (player/playermodule.h) is a module
#      like the rest (the Player page's widget is the shell's own page).
#
# Comment lines are dropped before matching: prose that names a forbidden
# pattern is not a use of it.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: shell_boundaries.sh <source-root>}"
cd "$ROOT" || { echo "source.shell_boundaries: no such root $ROOT"; exit 1; }

failures=0

strip_comments() { grep -nE "$1" "$2" 2>/dev/null | grep -vE '^[0-9]+:\s*//' ; }

# 1. modules/ and scripting/ never include the shell.
hits=""
while IFS= read -r f; do
    h=$(strip_comments '^\s*#\s*include\s+"shell/' "$f")
    [ -n "$h" ] && hits="$hits"$'\n'"$f: $h"
done < <(find src/modules src/scripting -name '*.cpp' -o -name '*.h' | sort)
if [ -n "$hits" ]; then
    echo "source.shell_boundaries: FAIL — a shell/ include under src/modules or src/scripting"
    echo "$hits" | sed '/^$/d; s/^/    /'
    echo "    the window is reached through ui/ishellview.h (IShellView), never the shell's headers."
    failures=1
else
    echo "source.shell_boundaries: ok — no shell/ include under src/modules or src/scripting"
fi

# 2. no literal stack index in the shell.
hits=""
while IFS= read -r f; do
    h=$(strip_comments 'setCurrentIndex\(\s*[0-9]' "$f")
    [ -n "$h" ] && hits="$hits"$'\n'"$f: $h"
done < <(find src/shell -name '*.cpp' -o -name '*.h' | sort)
if [ -n "$hits" ]; then
    echo "source.shell_boundaries: FAIL — setCurrentIndex(<digit>) in src/shell"
    echo "$hits" | sed '/^$/d; s/^/    /'
    echo "    pages are keyed by name: PageHost::show(\"<space>\")."
    failures=1
else
    echo "source.shell_boundaries: ok — no literal stack index in src/shell"
fi

# 3. mainwindow.cpp names no module header.
h=$(strip_comments '^\s*#\s*include\s+"(modules/|player/playermodule\.h)' src/shell/mainwindow.cpp \
    | grep -vE '"modules/(studiomodule|moduleregistry)\.h"')
if [ -n "$h" ]; then
    echo "source.shell_boundaries: FAIL — src/shell/mainwindow.cpp includes a module header by name"
    echo "$h" | sed 's/^/    /'
    echo "    the shell drives modules through StudioModule (shell/modulehub.h) and"
    echo "    builds them from modules/moduleregistry.cpp."
    failures=1
else
    echo "source.shell_boundaries: ok — mainwindow.cpp names no module header"
fi

exit $failures
