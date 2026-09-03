#!/usr/bin/env bash
#
# Builds a self-contained, redistributable Jahshaka.app from an existing
# non-Debug macOS build tree, and (optionally) packages it as a .dmg and a .zip.
#
# The result runs on a Mac with no Qt, no Vulkan SDK, no Xcode and no workspace:
# every dylib it needs lives inside the bundle, the Vulkan loader finds MoltenVK
# through an ICD manifest we ship with a bundle-relative library_path, and the
# whole thing is ad-hoc signed so Gatekeeper will let the user run it after one
# "Open Anyway".  Design, measurements and rejected alternatives:
# SPECS/MACOS_BUNDLE_SPEC.md.  Verify the result with scripts/verify-macos-bundle.sh.
#
#   ./scripts/make-macos-bundle.sh
#   BUILD_DIR=build-macos-rel DIST=dist ./scripts/make-macos-bundle.sh --no-dmg
#
# Idempotent: it deletes and rebuilds $DIST/Jahshaka.app from scratch every run.
# It NEVER copies the built bundle wholesale — that tree accumulates runtime
# droppings (jahsettings.ini, the logs, Tile.png, a dead nested downloader.app)
# and codesign refuses to seal data files under Contents/MacOS.
#
set -euo pipefail

# --- Inputs (env, all defaulted) --------------------------------------------
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-macos-rel}"
[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$REPO_ROOT/$BUILD_DIR"
QT_DIR="${QT_DIR:-$HOME/Qt/6.11.2/macos}"
OGRE_PREFIX="${OGRE_PREFIX:-$(dirname "$REPO_ROOT")/engines/ogre-next-install}"
DIST="${DIST:-$REPO_ROOT/dist}"
[[ "$DIST" = /* ]] || DIST="$REPO_ROOT/$DIST"
APP_VERSION="${APP_VERSION:-0.9.0}"
SIGN_ID="${SIGN_ID:--}"
THIN_ARM64="${THIN_ARM64:-1}"
MACOS_MIN="${MACOS_MIN:-13.0}"
MAKE_DMG=1
MAKE_ZIP=1

for arg in "$@"; do
    case "$arg" in
        --no-dmg) MAKE_DMG=0 ;;
        --no-zip) MAKE_ZIP=0 ;;
        --dmg)    MAKE_DMG=1 ;;
        --zip)    MAKE_ZIP=1 ;;
        --no-thin) THIN_ARM64=0 ;;
        -h|--help) sed -n '2,25p' "$0"; exit 0 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

die()  { echo "make-macos-bundle: $*" >&2; exit 1; }
step() { echo; echo "== $*"; }
check(){ echo "   check: $*"; }

[ "$(uname -s)" = "Darwin" ] || die "macOS only."

SRC_APP="$BUILD_DIR/bin/Jahshaka.app"
[ -x "$SRC_APP/Contents/MacOS/Jahshaka" ] || die \
    "no built app at $SRC_APP/Contents/MacOS/Jahshaka — build first (BUILD_DIR=<dir>)."
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
[ -x "$MACDEPLOYQT" ] || die "macdeployqt not found at $MACDEPLOYQT (set QT_DIR)."
[ -d "$OGRE_PREFIX/lib" ] || die "Ogre-Next install not found at $OGRE_PREFIX (set OGRE_PREFIX)."
[ -f "$OGRE_PREFIX/lib/RenderSystem_Vulkan.4.0.dylib" ] || die \
    "RenderSystem_Vulkan.4.0.dylib not in $OGRE_PREFIX/lib."
: "${VULKAN_SDK:?VULKAN_SDK not set — source the LunarG setup-env.sh first}"
[ -f "$VULKAN_SDK/lib/libMoltenVK.dylib" ] || die "libMoltenVK.dylib not in $VULKAN_SDK/lib."
[ -f "$VULKAN_SDK/lib/libvulkan.1.dylib" ] || die "libvulkan.1.dylib not in $VULKAN_SDK/lib."
[ -d "$BUILD_DIR/bin/media/Hlms/Pbs" ] || die \
    "engine media not staged at $BUILD_DIR/bin/media — build the engine target first."

# A Debug build writes jahsettings.ini into applicationDirPath(), i.e. INSIDE the
# signed bundle: the first launch would break its own signature. Refuse early.
if grep -q '^CMAKE_BUILD_TYPE:STRING=Debug$' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    die "$BUILD_DIR is a Debug build — a Debug bundle writes jahsettings.ini inside
    itself on first run and invalidates its own signature (src/data/settingsmanager.h).
    Configure a RelWithDebInfo or Release build dir and point BUILD_DIR at it."
fi

APP="$DIST/Jahshaka.app"
CONTENTS="$APP/Contents"

echo "make-macos-bundle"
echo "  source build : $BUILD_DIR"
echo "  Qt           : $QT_DIR"
echo "  Ogre-Next    : $OGRE_PREFIX"
echo "  Vulkan SDK   : $VULKAN_SDK"
echo "  output       : $APP"
echo "  version      : $APP_VERSION   sign: '$SIGN_ID'   thin-arm64: $THIN_ARM64"

# --- 1. Skeleton, not a copy -------------------------------------------------
step "1. clean skeleton"
rm -rf "$APP"
mkdir -p "$CONTENTS/MacOS" "$CONTENTS/Resources" "$CONTENTS/Frameworks"
cp "$SRC_APP/Contents/MacOS/Jahshaka" "$CONTENTS/MacOS/Jahshaka"
cp "$SRC_APP/Contents/Resources/icon.icns" "$CONTENTS/Resources/icon.icns"
[ "$(ls "$CONTENTS/MacOS")" = "Jahshaka" ] || die "step 1: Contents/MacOS is not just the binary"
check "Contents/MacOS holds exactly 'Jahshaka'"

# --- 2. Read-only content trees, in Resources where codesign can seal them ----
step "2. content trees -> Contents/Resources"
ditto "$REPO_ROOT/app"          "$CONTENTS/Resources/app"
ditto "$REPO_ROOT/scenes"       "$CONTENTS/Resources/scenes"
ditto "$BUILD_DIR/bin/media"    "$CONTENTS/Resources/media"
[ -d "$CONTENTS/Resources/media/Hlms/Pbs" ] || die "step 2: media/Hlms/Pbs missing"
[ -d "$CONTENTS/Resources/app/shader_defs" ] || die "step 2: app/shader_defs missing"
check "Resources/{app,scenes,media} present"

# --- 3. Symlinks back into MacOS/ -------------------------------------------
# IrisUtils::getAbsoluteAssetPath and the engine media probe both key off
# applicationDirPath() == Contents/MacOS. codesign seals these as symlinks and
# --deep --strict accepts them (MACOS_BUNDLE_SPEC §2.2), so no source changes.
step "3. Contents/MacOS symlinks into Resources"
for d in app scenes media; do
    ln -sfn "../Resources/$d" "$CONTENTS/MacOS/$d"
done
[ -d "$CONTENTS/MacOS/media/Hlms/Pbs" ] || die "step 3: symlink does not resolve"
check "MacOS/media/Hlms/Pbs resolves through the symlink"

# --- 4. Info.plist -----------------------------------------------------------
step "4. Info.plist"
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>            <string>en</string>
	<key>CFBundleDisplayName</key>                  <string>Jahshaka</string>
	<key>CFBundleExecutable</key>                   <string>Jahshaka</string>
	<key>CFBundleIconFile</key>                     <string>icon.icns</string>
	<key>CFBundleIdentifier</key>                   <string>com.jahshaka.studio</string>
	<key>CFBundleInfoDictionaryVersion</key>        <string>6.0</string>
	<key>CFBundleName</key>                         <string>Jahshaka</string>
	<key>CFBundlePackageType</key>                  <string>APPL</string>
	<key>CFBundleShortVersionString</key>           <string>$APP_VERSION</string>
	<key>CFBundleVersion</key>                      <string>$APP_VERSION</string>
	<key>LSApplicationCategoryType</key>            <string>public.app-category.graphics-design</string>
	<key>LSMinimumSystemVersion</key>               <string>$MACOS_MIN</string>
	<key>NSHighResolutionCapable</key>              <true/>
	<key>NSPrincipalClass</key>                     <string>NSApplication</string>
	<key>NSHumanReadableCopyright</key>             <string>Copyright © 2016-2026 EXEDOS LLC</string>
	<!-- Belt and braces: we use no capture API (src/services/audiopeaks.cpp is
	     QAudioDecoder only), but QtMultimedia's Darwin plugin enumerates devices
	     and macOS terminates a process that touches a TCC-guarded API with no
	     usage string. Cheaper than the crash report. -->
	<key>NSMicrophoneUsageDescription</key>         <string>Jahshaka does not record audio; this entry exists only because the media framework enumerates audio devices.</string>
	<key>NSCameraUsageDescription</key>             <string>Jahshaka does not record video; this entry exists only because the media framework enumerates video devices.</string>
</dict>
</plist>
PLIST
[ "$(defaults read "$CONTENTS/Info.plist" CFBundleIdentifier)" = "com.jahshaka.studio" ] \
    || die "step 4: CFBundleIdentifier did not take"
check "CFBundleIdentifier = com.jahshaka.studio"

# --- 5. macdeployqt ----------------------------------------------------------
# It follows link-time dependencies only: Qt frameworks + plugins, the FFmpeg
# dylibs, and all of OUR @rpath dylibs (IrisGL, assimp, Bullet, zip, the four
# OgreNext libs — Main, HlmsPbs, HlmsUnlit and Atmosphere, which fog links since
# it adopted the component's exponential fog) via the build-tree rpaths still
# baked into the copied binary,
# which it then collapses to @executable_path/../Frameworks. -no-codesign
# because we sign inside-out ourselves at step 10.
step "5. macdeployqt"
"$MACDEPLOYQT" "$APP" -verbose=1 -no-codesign
for p in PlugIns/platforms/libqcocoa.dylib PlugIns/sqldrivers/libqsqlite.dylib \
         Frameworks/QtCore.framework Resources/qt.conf; do
    [ -e "$CONTENTS/$p" ] || die "step 5: macdeployqt did not produce $p"
done
check "qcocoa, qsqlite, QtCore.framework, qt.conf present"

# macdeployqt deploys every SQL driver Qt was built with, and three of them are
# not self-contained: libqsqlodbc wants /opt/homebrew/.../libiodbc.2.dylib,
# libqsqlpsql wants /Applications/Postgres.app/.../libpq.5.dylib and libqsqlmimer
# wants /usr/local/lib/libmimerapi.dylib — all build-machine paths that simply do
# not exist on a user's Mac. Studio uses SQLite and nothing else
# (src/data/database.cpp), so these are dead weight AND holes in the audit.
step "5b. prune drivers that are not self-contained"
for dead in libqsqlodbc.dylib libqsqlpsql.dylib libqsqlmimer.dylib; do
    if [ -f "$CONTENTS/PlugIns/sqldrivers/$dead" ]; then
        rm -f "$CONTENTS/PlugIns/sqldrivers/$dead"
        echo "   - PlugIns/sqldrivers/$dead"
    fi
done
[ -f "$CONTENTS/PlugIns/sqldrivers/libqsqlite.dylib" ] || die "step 5b: removed the SQLite driver!"
check "sqldrivers holds $(ls "$CONTENTS/PlugIns/sqldrivers" | tr '\n' ' ')"

# --- 6. Engine payload macdeployqt cannot see (it is dlopen'd) ---------------
step "6. Vulkan render system + loader + MoltenVK"
cp "$OGRE_PREFIX/lib/RenderSystem_Vulkan.4.0.dylib" "$CONTENTS/Frameworks/"
# Ogre loads the plugin as pluginDir + "/RenderSystem_Vulkan" and appends ".dylib".
ln -sfn RenderSystem_Vulkan.4.0.dylib "$CONTENTS/Frameworks/RenderSystem_Vulkan.dylib"
cp -L "$VULKAN_SDK/lib/libvulkan.1.dylib" "$CONTENTS/Frameworks/libvulkan.1.dylib"
cp    "$VULKAN_SDK/lib/libMoltenVK.dylib" "$CONTENTS/Frameworks/libMoltenVK.dylib"
chmod u+w "$CONTENTS/Frameworks/RenderSystem_Vulkan.4.0.dylib" \
          "$CONTENTS/Frameworks/libvulkan.1.dylib" "$CONTENTS/Frameworks/libMoltenVK.dylib"
otool -L "$CONTENTS/Frameworks/RenderSystem_Vulkan.4.0.dylib" | grep -q '@rpath/libvulkan.1.dylib' \
    || die "step 6: RenderSystem_Vulkan does not link @rpath/libvulkan.1.dylib"
otool -L "$CONTENTS/Frameworks/RenderSystem_Vulkan.4.0.dylib" | grep -q '@rpath/libOgreNextMain.4.0.dylib' \
    || die "step 6: RenderSystem_Vulkan does not link @rpath/libOgreNextMain.4.0.dylib"
check "render system, loader and MoltenVK are in Frameworks with @rpath links intact"

# --- 7. ICD manifest with a bundle-relative library_path ---------------------
# The loader resolves a relative library_path against the MANIFEST's directory
# (measured, MACOS_BUNDLE_SPEC §2.3). Resources/vulkan/icd.d -> ../../../Frameworks.
# src/bridge/enginehost.cpp probes exactly this path first.
step "7. bundled ICD manifest"
mkdir -p "$CONTENTS/Resources/vulkan/icd.d"
SDK_ICD="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
ICD_API="1.4.0"
if [ -f "$SDK_ICD" ]; then
    # Take api_version from the SDK's own manifest so an SDK bump cannot leave us
    # silently understating the version.
    v=$(/usr/bin/sed -n 's/.*"api_version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$SDK_ICD" | head -1)
    [ -n "$v" ] && ICD_API="$v"
fi
cat > "$CONTENTS/Resources/vulkan/icd.d/MoltenVK_icd.json" <<ICD
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version": "$ICD_API",
        "is_portability_driver": true
    }
}
ICD
grep -q '"library_path": "\.\./' "$CONTENTS/Resources/vulkan/icd.d/MoltenVK_icd.json" \
    || die "step 7: library_path is not relative"
check "library_path is relative, api_version $ICD_API"

# --- 8. rpath surgery --------------------------------------------------------
# Every LC_RPATH that points outside the bundle is a hole in self-containment:
# on the build machine it silently keeps working, on a user's Mac it resolves to
# nothing. In practice this is exactly $VULKAN_SDK/lib on the OgreNext dylibs and
# the render system (baked by irisgl/scripts/build-ogre.sh); @loader_path is
# already there beside it, and Frameworks/ co-location does the rest.
step "8. strip out-of-bundle rpaths"
macho_files() {
    /usr/bin/find "$APP" -type f -print0 |
    while IFS= read -r -d '' f; do
        case "$(/usr/bin/file -b "$f")" in
            *Mach-O*) printf '%s\0' "$f" ;;
        esac
    done
}
fixed=0
while IFS= read -r -d '' f; do
    keeps=0
    while read -r rp; do
        [ -n "$rp" ] || continue
        case "$rp" in
            @loader_path*|@executable_path*) keeps=$((keeps+1)) ;;
            *) install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null \
                   && { echo "   - $rp   ($(basename "$f"))"; fixed=$((fixed+1)); } ;;
        esac
    done < <(otool -l "$f" | awk '/LC_RPATH/{r=1} r&&/ path /{print $2; r=0}')
    if [ "$keeps" -eq 0 ]; then
        install_name_tool -add_rpath @loader_path "$f" 2>/dev/null \
            && echo "   + @loader_path   ($(basename "$f"))"
    fi
done < <(macho_files)
check "$fixed out-of-bundle rpath entries removed"

# --- 9. Thin to arm64 --------------------------------------------------------
# Qt's kit, MoltenVK and the loader are universal; everything we build is
# arm64-thin, so half of Frameworks/ is dead x86_64 weight in an arm64-only
# product. THIN_ARM64=0 is the one-variable rollback.
if [ "$THIN_ARM64" = "1" ]; then
    step "9. lipo -thin arm64"
    thinned=0
    while IFS= read -r -d '' f; do
        if lipo -info "$f" 2>/dev/null | grep -q 'are: .*x86_64'; then
            tmp="$f.thin.$$"
            if lipo -thin arm64 "$f" -output "$tmp" 2>/dev/null; then
                chmod "$(stat -f '%Lp' "$f")" "$tmp"   # preserve mode; replace in place
                mv -f "$tmp" "$f"
                thinned=$((thinned+1))
            else
                rm -f "$tmp"
            fi
        fi
    done < <(macho_files)
    check "$thinned universal Mach-O files thinned to arm64"
else
    step "9. lipo -thin arm64 SKIPPED (THIN_ARM64=0)"
fi

# --- 10. Sign, inside-out ----------------------------------------------------
# --deep is deprecated for SIGNING since macOS 13 (it applies every option to
# every nested item); it stays valid for VERIFICATION. So: loose dylibs first,
# then plugin bundles, then frameworks, then the app last.
step "10. codesign (inside-out, id '$SIGN_ID')"
sign() { codesign --force --sign "$SIGN_ID" --timestamp=none "$1" >/dev/null 2>&1 \
         || die "codesign failed on $1"; }
while IFS= read -r -d '' f; do
    case "$f" in
        */Contents/MacOS/Jahshaka) continue ;;   # signed with the app bundle
        *.framework/*) continue ;;               # signed as part of the framework
    esac
    sign "$f"
done < <(macho_files)
for fw in "$CONTENTS/Frameworks"/*.framework; do
    [ -d "$fw" ] && sign "$fw"
done
sign "$APP"
codesign -v --deep --strict --verbose=2 "$APP" 2>&1 | tail -2
# Capture first: `codesign … | grep -q` would SIGPIPE codesign, and `set -o
# pipefail` then reports the whole pipeline as failed even on a match.
sig_info="$(codesign -dvvv "$APP" 2>&1 || true)"
echo "$sig_info" | grep -E '^(Identifier|Sealed Resources|Signature)' || true
case "$sig_info" in
    *"Identifier=com.jahshaka.studio"*) ;;
    *) die "step 10: bundle identifier did not survive signing" ;;
esac
check "signature valid, Identifier=com.jahshaka.studio"

echo
echo "bundle size: $(du -sh "$APP" | cut -f1)"

# --- 11. Package -------------------------------------------------------------
if [ "$MAKE_DMG" = "1" ]; then
    step "11a. dmg"
    DMG="$DIST/Jahshaka-$APP_VERSION-arm64.dmg"
    STAGE="$DIST/.dmg-stage"
    rm -rf "$STAGE"; mkdir -p "$STAGE"
    ditto "$APP" "$STAGE/Jahshaka.app"
    ln -s /Applications "$STAGE/Applications"
    rm -f "$DMG"
    hdiutil create -volname "Jahshaka $APP_VERSION" -srcfolder "$STAGE" \
                   -ov -format UDZO -quiet "$DMG"
    rm -rf "$STAGE"
    check "$(basename "$DMG") $(du -h "$DMG" | cut -f1)"
fi
if [ "$MAKE_ZIP" = "1" ]; then
    step "11b. zip"
    ZIP="$DIST/Jahshaka-$APP_VERSION-arm64.zip"
    rm -f "$ZIP"
    ( cd "$DIST" && ditto -c -k --sequesterRsrc --keepParent "Jahshaka.app" "$ZIP" )
    check "$(basename "$ZIP") $(du -h "$ZIP" | cut -f1)"
fi

echo
echo "done: $APP"
echo "verify with: scripts/verify-macos-bundle.sh \"$APP\""
