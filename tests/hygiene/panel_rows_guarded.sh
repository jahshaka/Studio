#!/usr/bin/env bash
#
# source.panel_rows_guarded — A PANEL THAT RETIRES ROWS HOLDS NO RAW POINTER TO
# ONE (lane PANEL-LIFETIME-1).
#
# THE CLASS, measured. `AccordianBladeWidget::clearPanel` retires a row by
# taking it out of the content pane, hiding it and calling deleteLater(); the
# row dies at the next turn of the event loop, and the loop turns in places the
# panels never had in mind — between the threaded open's install stages
# (services/sceneopenrunner.h), between a script run's verbs (a worker thread
# and a nested loop), and wherever a panel defers its own rebuild (the sky
# panel does, on purpose). The sky panel kept a raw `TexturePickerWidget *` to
# the row of a sky type it had rebuilt away from, and `setEquiMap` — a sky
# preset, a dropped image, an asset pick — tested it for null and wrote through
# it: ui.panel_lifetime reproduces that as a heap-use-after-free under ASan
# (READ of size 8 in TexturePickerWidget::setTexture, freed by
# ~TexturePickerWidget from a DeferredDelete).
#
# So: a row handle in such a panel is a RowPtr (src/ui/controls/bladerow.h),
# which reads null from the moment the row is retired. This gate keeps the raw
# pointers from growing back — and, because it looks at the files that CALL
# clearPanel, it also catches a panel that starts retiring rows tomorrow while
# still holding them raw.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: panel_rows_guarded.sh <source-root>}"
cd "$ROOT" || { echo "source.panel_rows_guarded: no such root $ROOT"; exit 1; }

failures=0

# The row/control types the blades hand out (AccordianBladeWidget::add*() and
# PropertyWidget::add*()). A pointer to one of these, held as a member by a
# panel that retires rows, is the defect.
rowtypes='ComboBoxWidget|CheckBoxWidget|HFloatSliderWidget|ColorValueWidget|TexturePickerWidget|FilePickerWidget|TextInputWidget|LabelWidget|DragFloatWidget|DragVector3Widget|DragSpinBox|CubeMapWidget|PropertyWidget|AccordianBladeWidget|TransformEditor|LightChannelsWidget|ParticleColourRampWidget|ParticleScaleRampWidget|QPushButton'

# Every implementation file that retires rows, and the header beside it. A
# clearPanel() named in PROSE does not count (the properties panel explains the
# retirement in three comments and retires nothing itself).
clearers=$(grep -rnE '(^|[^[:alnum:]_])clearPanel[[:space:]]*\(\)' src/ --include=*.cpp 2>/dev/null \
           | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' \
           | cut -d: -f1 | sort -u \
           | grep -v 'src/ui/controls/accordionbladewidget.cpp' || true)

if [ -z "$clearers" ]; then
    echo "FAIL: nothing in src/ calls clearPanel() any more — this gate has lost"
    echo "      its subject; re-aim it or delete it with the retirement path."
    exit 1
fi

for cpp in $clearers; do
    header="${cpp%.cpp}.h"
    [ -f "$header" ] || continue
    # `Type *name;` / `Type *name = nullptr;` as a MEMBER. A parameter ends in
    # `,` or `)`; a comment line starts with // or *; a RowPtr<Type> member
    # never matches (no `*`).
    hits=$(grep -nE "(^|[^:_[:alnum:]])($rowtypes)[[:space:]]*\*[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*(=[[:space:]]*(nullptr|0|Q_NULLPTR))?[[:space:]]*;" "$header" 2>/dev/null \
           | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' || true)
    if [ -n "$hits" ]; then
        echo "FAIL: $header holds a RAW pointer to a row, and $cpp retires rows."
        echo "      Hold it as RowPtr<T> (src/ui/controls/bladerow.h): a retired"
        echo "      row must read as null, not as a pointer that is about to be"
        echo "      freed by the next turn of the event loop."
        echo "$hits"
        failures=$((failures + 1))
    else
        echo "ok:   $(basename "$header") holds its rows as handles"
    fi
done

# The mark itself: clearPanel is the only thing that may set it, and it must.
if grep -q 'bladerow::markRetired' src/ui/controls/accordionbladewidget.cpp; then
    echo "ok:   the blade marks every row it retires"
else
    echo "FAIL: AccordianBladeWidget no longer marks the rows it retires —"
    echo "      every RowPtr in the tree silently goes back to being a raw"
    echo "      pointer that only nulls at destruction."
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "source.panel_rows_guarded: $failures failure(s)"
    exit 1
fi
echo "source.panel_rows_guarded: ok"
