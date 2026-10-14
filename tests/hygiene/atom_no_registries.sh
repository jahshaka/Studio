#!/usr/bin/env bash
#
# source.atom_no_registries — ATOM KEEPS NO PROCESS-WIDE STATE (ENGINE-UPGRADE-2 stage 3).
#
# The Atom passes kept their state in process maps keyed by pointers — which view a
# workspace was, which scene a SceneManager was, each view's and each scene's stats ring,
# the scenes' caster test doors, the pipelines, the pyramid's counter, the log-once
# memories. Each now lives with its owner: the scene's in its AtomComponent
# (atom/AtomComponent.h), a view's ring in the view, the device's objects in Atom's Hlms
# (HlmsAtom::idDevice / casterDevice), the counter on its compute job's own binding; a
# pass finds its scene bound on its SceneManager (sceneOfManager) and its view through its
# workspace's listener. This counts the MUTABLE storage with static duration left in
# Atom's files — a namespace-scope variable definition, or a function-local `static` that
# is not const — and the bar is 0. The one exception is named: the pass provider
# (OgreAtomPass.cpp's gProvider / sProvider), Ogre's one CompositorPassProvider per
# CompositorManager2, which the engine installs and uninstalls (AtomPass.h).
#
# $1 = the repo root
set -u

ROOT="${1:?usage: atom_no_registries.sh <source-root>}"
SRC="$ROOT/irisgl/engine/src"
files=(OgreAtomDraw.cpp OgreAtomIdPass.cpp OgreAtomCasterPass.cpp OgreAtomPass.cpp HlmsAtom.cpp
       atom/AtomComponent.cpp)
n=0
for f in "${files[@]}"; do
    [ -f "$SRC/$f" ] || { echo "source.atom_no_registries: FAIL — no $SRC/$f"; exit 1; }
    hits=$(awk -v F="$f" '
        # a function-local (or any indented) static that is not const: storage
        /^[[:space:]]+static[[:space:]]/ && !/static[[:space:]]+(const|constexpr|inline)[[:space:]]/ &&
            !/static_assert|static_cast/ && !/\(/ { print F ":" NR ": " $0; next }
        # a namespace-scope variable definition: column 0, ends in ; (or = ...;), no call/declaration shape
        /^[A-Za-z_]/ && /;[[:space:]]*(\/\/.*)?$/ && !/\(/ &&
            !/^(using|typedef|struct|class|namespace|enum|template|extern|friend|constexpr|const |static_assert|return|#)/ \
            { print F ":" NR ": " $0 }
    ' "$SRC/$f" | grep -vE 'OgreAtomPass\.cpp:[0-9]+: (AtomPassProvider \*gProvider = nullptr;|    static AtomPassProvider sProvider;)$')
    if [ -n "$hits" ]; then
        echo "$hits" >&2
        n=$(( n + $(printf '%s\n' "$hits" | wc -l) ))
    fi
done
if [ "$n" = 0 ]; then
    echo "source.atom_no_registries: ok — Atom's files keep no process-wide mutable state (the provider aside)"
    exit 0
fi
echo "source.atom_no_registries: FAIL — $n process-wide variable(s) in Atom's files (above): the state"
echo "    goes to its owner (the scene's AtomComponent, the view, HlmsAtom's device state), never a static"
exit 1
