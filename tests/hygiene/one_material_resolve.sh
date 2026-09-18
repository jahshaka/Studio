#!/usr/bin/env bash
#
# source.one_material_resolve — THERE IS ONE WAY TO RESOLVE A MATERIAL, and it
# is not a QVariant on the AssetManager (MATERIAL-PREVIEW-1).
#
# WHY THIS NEEDS A GATE RATHER THAN A CONVENTION. The editor's live hover
# preview died on a STATIC TYPE MISMATCH INSIDE A QVariant: three registration
# sites stored the hydrated material as `QSharedPointer<PbrMaterial>` (through
# `auto`, so nobody wrote the type down) while the one reader asked for
# `iris::MaterialPtr`. Qt has no converter between those two — Material is not a
# QObject — so the read came back null and the preview was skipped, silently,
# for every material source but one. The drop re-resolved the guid from the
# database and worked, which is exactly why it read as a preview bug for weeks.
#
# Nothing about that is visible at a call site: `setValue(QVariant::fromValue(m))`
# and `value<iris::MaterialPtr>()` both compile, both look right, and the defect
# only appears as a feature that does nothing. The cure was to delete the
# payload entirely — SceneEditService::resolveMaterial reads the preset list and
# the database, which is what the DROP always did — and the guard is here so a
# future "just park it in the AssetManager, it's cheaper" cannot reintroduce it.
#
# THE RULE: no source file may put a MATERIAL POINTER into an Asset's QVariant.
# AssetMaterial rows still exist (they carry a guid and a name, and the Shader
# flavour carries its JSON definition) — it is the hydrated material that is
# banned.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: one_material_resolve.sh <source-root>}"
cd "$ROOT" || { echo "source.one_material_resolve: no such root $ROOT"; exit 1; }

failures=0

# A material pointer handed to QVariant::fromValue. The names are the ones this
# tree uses for a material: `material`, `mat`, `m`, and the two typedefs
# spelled out. A JSON `definition`/`shaderDefinition` is NOT one of them and is
# what the Shader-flavoured AssetMaterial legitimately carries.
hits=$(grep -rnE 'QVariant::fromValue\([[:space:]]*(iris::(Material|PbrMaterial)Ptr\(|(material|mat|m|pbr|newMaterial)[[:space:]]*\))' \
           src irisgl --include=*.cpp --include=*.h \
       | grep -v '^tests/' || true)

if [ -n "$hits" ]; then
    echo "source.one_material_resolve: FAIL — a hydrated material put into a QVariant payload"
    echo "$hits" | sed 's/^/    /'
    echo "    Resolve materials through SceneEditService::resolveMaterial (the preset list"
    echo "    plus the database) — the AssetManager carries guids and names, not materials."
    echo "    A QVariant payload cannot be type-checked: this is the exact shape that made"
    echo "    the live hover preview a silent no-op for two of its three sources."
    failures=1
else
    echo "source.one_material_resolve: ok — no material pointer is parked in an Asset variant"
fi

# ...and the reader that used to do it must stay gone. If anything asks an
# Asset for a MaterialPtr again, the mismatch is back whatever the writers do.
readers=$(grep -rn 'value<iris::MaterialPtr>()' src irisgl --include=*.cpp --include=*.h || true)
if [ -n "$readers" ]; then
    echo "source.one_material_resolve: FAIL — a QVariant is being read back as a material"
    echo "$readers" | sed 's/^/    /'
    failures=1
fi

# THE RESOLVER MUST STILL BE ONE. Routing through it means nothing if it stops
# being the single entry point.
if ! grep -q 'iris::MaterialPtr resolveMaterial(const QString &presetOrGuid) const;' \
        src/services/sceneeditservice.h; then
    echo "source.one_material_resolve: FAIL — SceneEditService::resolveMaterial is gone or has changed shape"
    failures=1
fi

exit $failures
