#!/usr/bin/env bash
#
# source.one_fonticons — ONE QtAwesome PER PROCESS, AND ONE COPY OF IT IN THE
# TREE (QTAWESOME-1, UNINIT_SWEEP_SPEC §7).
#
# WHAT WAS WRONG. `src/modules/materials/misc/QtAwesome{,Anim}.{h,cpp}` was a
# second, byte-identical copy of `thirdparty/qtawesome/` (the only difference:
# the include paths and a licence header). No CMakeLists compiled the copy's
# .cpp files — they sat commented out in cmake/IncludeMaterialsModule.cmake —
# so the class the module's two headers declared was linked from the vendored
# TU: a duplicate that could not be seen from either side, and would have been
# an ODR violation the day either copy was edited.
#
# And the INSTANCES were the real cost. `QtAwesome::initFontAwesome()` loads
# the font once per process (the font id is a function-local static) but fills
# THIS INSTANCE'S hash with 786 name-to-codepoint entries, and nothing in this
# tree ever deleted an instance: the shell made one, the materials page made a
# second, and BasePropertyWidget made one PER PROPERTY ROW — each with its own
# 786-entry hash, leaked, and never asked for a single icon (the rows' icons
# are .png resources).
#
# THE RULE, and it is two lines:
#   1. no second copy of the vendored QtAwesome sources anywhere in the tree;
#   2. exactly ONE `new QtAwesome` in src/ — fonticons::shared()'s. Everything
#      that draws a glyph icon takes that reference (src/ui/controls/fonticons.h).
#
# $1 = the repo root
set -u

ROOT="${1:?usage: one_fonticons.sh <source-root>}"
cd "$ROOT" || { echo "source.one_fonticons: no such root $ROOT"; exit 1; }

failures=0

# 1. THE SOURCES. thirdparty/qtawesome is the one copy; a QtAwesome*.cpp/.h
#    anywhere else is a fork of vendored code by another name.
copies=$(find src irisgl -name 'QtAwesome*.cpp' -o -name 'QtAwesome*.h' 2>/dev/null || true)
if [ -n "$copies" ]; then
    echo "source.one_fonticons: FAIL — a second copy of the vendored QtAwesome sources"
    echo "$copies" | sed 's/^/    /'
    echo "    thirdparty/qtawesome/ is the one copy (vendored, never edited);"
    echo "    include it as \"thirdparty/qtawesome/QtAwesome.h\"."
    failures=1
else
    echo "source.one_fonticons: ok — thirdparty/qtawesome is the only copy of it"
fi

# 2. THE INSTANCE. One construction site, and it is the shared accessor's.
# (Comment lines are dropped: this file's own prose, and the note left where the
# per-row instance used to be, both name the expression they forbid.)
sites=$(grep -rn 'new QtAwesome' src --include=*.cpp --include=*.h \
        | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)' || true)
count=$(printf '%s' "$sites" | grep -c . || true)
expected='src/ui/controls/fonticons.cpp'
if [ "$count" != "1" ] || ! printf '%s' "$sites" | grep -q "^$expected:"; then
    echo "source.one_fonticons: FAIL — a QtAwesome is constructed outside fonticons::shared()"
    printf '%s\n' "$sites" | sed 's/^/    /'
    echo "    Every glyph icon comes from fonticons::shared() (src/ui/controls/fonticons.h):"
    echo "    one font load, one 786-entry codepoint map, for the whole process."
    failures=1
else
    echo "source.one_fonticons: ok — exactly one QtAwesome is constructed, in $expected"
fi

exit $failures
