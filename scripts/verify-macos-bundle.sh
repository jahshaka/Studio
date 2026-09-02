#!/usr/bin/env bash
#
# The self-containment gate for a bundle produced by scripts/make-macos-bundle.sh.
#
# Two halves, both of which must pass:
#
#   STATIC   every Mach-O in the bundle links and rpath-searches only inside the
#            bundle, /usr/lib or /System/Library; the ICD manifest is relative;
#            the signature is valid; the deployment floor is what we claim.
#   RUNTIME  a COPY of the bundle, outside the workspace, run with an empty
#            environment and a scratch home, renders the default scene
#            (--engine-selftest) through the MoltenVK it ships, and the signature
#            is still valid afterwards (i.e. it wrote nothing inside itself).
#
#   ./scripts/verify-macos-bundle.sh [dist/Jahshaka.app]
#
# We cannot test a genuinely clean Mac from here. env -i plus a scratch home plus
# the static allow-list is the strongest available proof; what it cannot catch is
# a dependency satisfied by something outside $HOME and outside the allow-list —
# which is why the allow-list is /usr/lib + /System/Library and nothing else.
# SPECS/MACOS_BUNDLE_SPEC.md §5.2, §6.3, §7.
#
# NOTE: the runtime half opens a real window for about a second. macOS has no
# Xvfb; --engine-selftest calls window.show() and the on-screen assertion in
# src/app/cli/selftestrunner.cpp is the whole point of running it. Run this at
# gates, not casually.
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUNDLE="${1:-$REPO_ROOT/dist/Jahshaka.app}"
BUNDLE="$(cd "$(dirname "$BUNDLE")" && pwd)/$(basename "$BUNDLE")"
EXPECT_MINOS="${EXPECT_MINOS:-13.0}"
EXPECT_ID="${EXPECT_ID:-com.jahshaka.studio}"

fails=0
pass() { echo "  PASS  $*"; }
fail() { echo "  FAIL  $*"; fails=$((fails+1)); }
warn() { echo "  warn  $*"; }
head2() { echo; echo "== $*"; }

[ -d "$BUNDLE" ] || { echo "verify-macos-bundle: no bundle at $BUNDLE" >&2; exit 2; }
echo "verify-macos-bundle: $BUNDLE"

macho_files() {
    /usr/bin/find "$1" -type f -print0 |
    while IFS= read -r -d '' f; do
        case "$(/usr/bin/file -b "$f")" in *Mach-O*) printf '%s\0' "$f" ;; esac
    done
}

# otool -L prints ONE header line per architecture in a universal binary
# ("<path> (architecture arm64):"), and only the dependency lines are tab-indented.
# Anything that keys off `tail -n +2` reads a fat binary's own path as a dependency.
otool_deps() { otool -L "$1" | grep '^	' | awk '{print $1}' | sort -u; }

# ---------------------------------------------------------------- static audit
head2 "static: linked libraries"
bad=0; nmacho=0
while IFS= read -r -d '' f; do
    nmacho=$((nmacho+1))
    while read -r dep; do
        case "$dep" in
            @rpath/*|@loader_path/*|@executable_path/*|/usr/lib/*|/System/Library/*) ;;
            "") ;;
            *) fail "$(basename "$f") links out-of-bundle: $dep"; bad=$((bad+1)) ;;
        esac
    done < <(otool_deps "$f")
done < <(macho_files "$BUNDLE")
[ "$nmacho" -gt 0 ] || fail "no Mach-O files found in the bundle at all"
[ "$bad" -eq 0 ] && pass "$nmacho Mach-O files, every dependency is @rpath/@loader_path/@executable_path, /usr/lib or /System/Library"

head2 "static: rpath search paths"
bad=0
while IFS= read -r -d '' f; do
    while read -r rp; do
        [ -n "$rp" ] || continue
        case "$rp" in
            @loader_path*|@executable_path*) ;;
            "$BUNDLE"*) ;;
            *) fail "$(basename "$f") has an out-of-bundle LC_RPATH: $rp"; bad=$((bad+1)) ;;
        esac
    done < <(otool -l "$f" | awk '/LC_RPATH/{r=1} r&&/ path /{print $2; r=0}')
done < <(macho_files "$BUNDLE")
[ "$bad" -eq 0 ] && pass "no LC_RPATH entry points outside the bundle"

head2 "static: @rpath dependencies resolve inside Contents/Frameworks"
missing=0
while IFS= read -r -d '' f; do
    while read -r dep; do
        case "$dep" in @rpath/*) ;; *) continue ;; esac
        rel="${dep#@rpath/}"
        # A framework dep is @rpath/QtCore.framework/Versions/A/QtCore.
        [ -e "$BUNDLE/Contents/Frameworks/$rel" ] && continue
        # Its own install_name counts as satisfied by the file itself.
        [ "$(basename "$rel")" = "$(basename "$f")" ] && continue
        fail "$(basename "$f") wants @rpath/$rel — not in Contents/Frameworks"
        missing=$((missing+1))
    done < <(otool_deps "$f")
done < <(macho_files "$BUNDLE")
[ "$missing" -eq 0 ] && pass "every @rpath dependency has a file in Contents/Frameworks"

head2 "static: bundled Vulkan payload"
for f in Contents/Frameworks/libvulkan.1.dylib Contents/Frameworks/libMoltenVK.dylib \
         Contents/Frameworks/RenderSystem_Vulkan.4.0.dylib \
         Contents/Frameworks/RenderSystem_Vulkan.dylib \
         Contents/Resources/vulkan/icd.d/MoltenVK_icd.json; do
    [ -e "$BUNDLE/$f" ] && pass "$f" || fail "$f is missing"
done
ICD="$BUNDLE/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json"
if [ -f "$ICD" ]; then
    if grep -q '"library_path"[[:space:]]*:[[:space:]]*"/' "$ICD"; then
        fail "ICD library_path is absolute"
    else
        pass "ICD library_path is bundle-relative: $(sed -n 's/.*"library_path"[^"]*"\([^"]*\)".*/\1/p' "$ICD")"
    fi
fi

head2 "static: content trees"
for d in Contents/Resources/app Contents/Resources/scenes Contents/Resources/media/Hlms/Pbs \
         Contents/MacOS/app Contents/MacOS/scenes Contents/MacOS/media/Hlms/Pbs; do
    [ -e "$BUNDLE/$d" ] && pass "$d" || fail "$d is missing"
done
# codesign refuses to seal data files that are real (non-symlink) entries under
# Contents/MacOS — that is the failure macdeployqt hit before the trees moved.
stray=$(/usr/bin/find "$BUNDLE/Contents/MacOS" -maxdepth 1 -mindepth 1 ! -type l ! -name Jahshaka | wc -l | tr -d ' ')
[ "$stray" = "0" ] && pass "Contents/MacOS holds the executable and symlinks only" \
                   || fail "$stray non-symlink strays under Contents/MacOS"

head2 "static: deployment target"
minos=$(otool -l "$BUNDLE/Contents/MacOS/Jahshaka" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
[ "$minos" = "$EXPECT_MINOS" ] && pass "executable minos $minos" \
                              || fail "executable minos $minos, expected $EXPECT_MINOS"
worst=""
while IFS= read -r -d '' f; do
    m=$(otool -l "$f" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
    [ -n "$m" ] || continue
    # crude but adequate: 26.0 sorts above 13.0 numerically
    if awk -v a="$m" -v b="$EXPECT_MINOS" 'BEGIN{exit !(a+0 > b+0)}'; then
        worst="$worst $(basename "$f"):$m"
    fi
done < <(macho_files "$BUNDLE")
[ -z "$worst" ] && pass "no Mach-O in the bundle demands more than macOS $EXPECT_MINOS" \
                || fail "these raise the real floor above $EXPECT_MINOS:$worst"

head2 "static: architectures"
archs=$(while IFS= read -r -d '' f; do lipo -archs "$f" 2>/dev/null; done < <(macho_files "$BUNDLE") | tr ' ' '\n' | sort -u | tr '\n' ' ')
pass "architectures present: $archs"

head2 "static: signature"
if codesign -v --deep --strict --verbose=2 "$BUNDLE" >/dev/null 2>&1; then
    pass "codesign -v --deep --strict"
else
    fail "codesign -v --deep --strict: $(codesign -v --deep --strict "$BUNDLE" 2>&1 | tail -1)"
fi
ident=$(codesign -dvvv "$BUNDLE" 2>&1 | sed -n 's/^Identifier=//p')
[ "$ident" = "$EXPECT_ID" ] && pass "Identifier=$ident" || fail "Identifier=$ident, expected $EXPECT_ID"
codesign -dvvv "$BUNDLE" 2>&1 | grep -q 'Sealed Resources version=2' \
    && pass "Sealed Resources version=2" || fail "resources are not sealed"

# --------------------------------------------------------------- runtime proof
head2 "runtime: hermetic --engine-selftest on a copy"
# pwd -P: /tmp is a symlink to /private/tmp and the Vulkan loader logs the REAL
# path, so an unresolved $GATE would never match its output.
GATE=$(cd "$(mktemp -d /tmp/jah-bundle-gate.XXXXXX)" && pwd -P)
trap 'rm -rf "$GATE"' EXIT
ditto "$BUNDLE" "$GATE/Jahshaka.app"
mkdir -p "$GATE/home" "$GATE/tmp"

# CFFIXED_USER_HOME as well as HOME: QStandardPaths::AppDataLocation on macOS goes
# through NSFileManager and does NOT follow $HOME (measured 2026-09-02 — a run with
# only HOME set wrote its library DB and Ogre log into the REAL
# ~/Library/Application Support/Jahshaka). CFFIXED_USER_HOME is what CoreFoundation
# honours, and with it set the whole AppDataLocation tree lands in $GATE/home.
#
# Run it with cwd inside $GATE: the app drops a Tile.png (and, in a Debug build,
# jahsettings.ini and the logs) into the working directory, and the working
# directory here would otherwise be the repo.
(
  cd "$GATE" || exit 99
  env -i PATH=/usr/bin:/bin HOME="$GATE/home" CFFIXED_USER_HOME="$GATE/home" \
      TMPDIR="$GATE/tmp" VK_LOADER_DEBUG=all DYLD_PRINT_LIBRARIES=1 \
      "$GATE/Jahshaka.app/Contents/MacOS/Jahshaka" --engine-selftest "$GATE/out.png" \
      > "$GATE/stdout.log" 2> "$GATE/stderr.log"
)
rc=$?

# 1. exit status
[ "$rc" -eq 0 ] && pass "exit status 0" || fail "exit status $rc (see below)"

# 2. a real render
if [ -f "$GATE/out.png" ]; then
    sz=$(stat -f %z "$GATE/out.png")
    kind=$(/usr/bin/file -b "$GATE/out.png")
    if [ "$sz" -gt 4096 ] && [[ "$kind" == PNG* ]]; then
        pass "out.png is a $sz-byte PNG"
    else
        fail "out.png is $sz bytes / $kind"
    fi
else
    fail "no out.png was produced"
fi

# 3. the on-screen view really was on-screen
grep -q 'engine-selftest: on-screen view survived resizes:' "$GATE/stderr.log" \
    && pass "$(grep -m1 'on-screen view survived' "$GATE/stderr.log")" \
    || fail "the on-screen assertion did not print (offscreen fallback?)"
grep -q 'centre pixel' "$GATE/stderr.log" && \
    pass "$(grep -m1 'centre pixel' "$GATE/stderr.log" | sed 's/^engine-selftest: //')"

# 4. it was OUR manifest and OUR MoltenVK that loaded
if grep -q "Found ICD manifest file $GATE/Jahshaka.app/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" "$GATE/stderr.log"; then
    pass "loader found the BUNDLED ICD manifest"
else
    fail "the bundled ICD manifest was not the one the loader used"
fi
if grep -q 'VulkanSDK' "$GATE/stderr.log"; then
    fail "the run mentions VulkanSDK — it reached the SDK on this machine"
else
    pass "no VulkanSDK reference anywhere in the run"
fi

# 5. dyld corroboration (soft: DOCS/MACOS_BUILD.md §5 records DYLD_* being stripped
#    across /bin/bash under ctest, so absence of output is not a failure)
if grep -q 'DYLD_PRINT_LIBRARIES\|dyld\[' "$GATE/stderr.log" || grep -q "$GATE/Jahshaka.app" "$GATE/stderr.log"; then
    if grep -q "$GATE/Jahshaka.app/Contents/Frameworks/libMoltenVK.dylib" "$GATE/stderr.log"; then
        pass "dyld loaded the bundle's own libMoltenVK.dylib"
    else
        warn "dyld output present but libMoltenVK.dylib not named in it"
    fi
else
    warn "DYLD_PRINT_LIBRARIES produced nothing — corroboration skipped"
fi

# 6. nothing outside the bundle was loaded from a user home
if grep -qE '/Users/[^ ]*\.dylib' "$GATE/stderr.log"; then
    fail "a dylib under /Users was loaded: $(grep -oE '/Users/[^ ]*\.dylib' "$GATE/stderr.log" | sort -u | head -3 | tr '\n' ' ')"
else
    pass "no dylib under /Users was loaded"
fi

# 7. the scratch home really did displace the developer's SDK probe
if [ -z "$(/usr/bin/find "$GATE/home" -name 'VulkanSDK*' 2>/dev/null)" ]; then
    pass "the scratch home contains no VulkanSDK (enginehost's \$HOME probe was defeated)"
else
    fail "the scratch home contains a VulkanSDK — the probe was not exercised"
fi
if [ -d "$GATE/home/Library/Application Support/Jahshaka" ]; then
    pass "user data went to the scratch home ($(ls "$GATE/home/Library/Application Support/Jahshaka" | tr '\n' ' '))"
else
    warn "no user data in the scratch home — CFFIXED_USER_HOME may not have taken"
fi

# 8. the run wrote nothing inside the bundle
if codesign -v --deep --strict "$GATE/Jahshaka.app" >/dev/null 2>&1; then
    pass "signature still valid AFTER the run (nothing was written inside the bundle)"
else
    fail "the run invalidated the signature: $(/usr/bin/find "$GATE/Jahshaka.app" -newer "$GATE/home" -type f | head -5 | tr '\n' ' ')"
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "verify-macos-bundle: PASS  ($(du -sh "$BUNDLE" | cut -f1) bundle)"
    exit 0
fi
echo "verify-macos-bundle: FAIL — $fails failed assertion(s)"
echo "--- tail of the run's stderr ---"
tail -40 "$GATE/stderr.log"
exit 1
