#!/usr/bin/env bash
#
# source.no_glsl_path — the GLSL load path stays deleted.
#
# HLMS_ADOPTION_SPEC §5 (P3) removed the document-side GLSL half: the
# include-expanding loader (GraphicsHelper::loadAndProcessShader), the source
# carrier (iris::Shader), Material's shader/setShader/createProgramFromShaderSource,
# and app/shaders/ + its qrc row. Nothing ever read the loaded text back — it
# was 168 KB of shader source and a per-line regex preprocessor running on
# every scene open for a build with no GL.
#
# The evaluator program's phase-5 precedent: a deletion of that size needs a
# gate, or the names creep back one call site at a time. This asserts the names
# are absent from the source tree (archive/, thirdparty/ and build dirs
# excluded — the archive is history and vendored code has its own unrelated
# GL/D3D methods by the same names).
#
# $1 = the repo root
set -u

ROOT="${1:?usage: no_glsl_path.sh <source-root>}"
cd "$ROOT" || { echo "source.no_glsl_path: no such root $ROOT"; exit 1; }

failures=0

# The names that must not come back, and where they were.
#   loadAndProcessShader            irisgl/import/graphicshelper.cpp
#   createProgramFromShaderSource   irisgl/document/materials/material.cpp
#   iris::Shader / ShaderPtr        irisgl/document/assets/shader.{h,cpp}
PATTERNS=(
    'loadAndProcessShader'
    'createProgramFromShaderSource'
    'ShaderPtr'
    'document/assets/shader\.h'
)

# app/shaders/ is a PATH, not a symbol: the six app/shader_defs/*.shader files
# still name files under it in their (deliberately unread) vertex_shader /
# fragment_shader keys, so the path check is that the DIRECTORY is gone and no
# build file references the qrc.
SEARCH_DIRS=(src irisgl/core irisgl/document irisgl/engine irisgl/import
             irisgl/mirror tests app/themes cmake)

for pat in "${PATTERNS[@]}"; do
    hits=$(grep -rn --include='*.cpp' --include='*.h' --include='*.qrc' \
                 --include='*.txt' --include='*.cmake' \
                 -E "$pat" "${SEARCH_DIRS[@]}" 2>/dev/null || true)
    if [ -n "$hits" ]; then
        echo "FAIL: '$pat' is back in the tree:"
        echo "$hits"
        failures=$((failures + 1))
    else
        echo "ok:   no '$pat'"
    fi
done

if [ -d app/shaders ]; then
    echo "FAIL: app/shaders/ exists again"
    failures=$((failures + 1))
else
    echo "ok:   no app/shaders/ directory"
fi

if [ -f app/shaders.qrc ]; then
    echo "FAIL: app/shaders.qrc exists again"
    failures=$((failures + 1))
else
    echo "ok:   no app/shaders.qrc"
fi

if grep -q 'shaders\.qrc' CMakeLists.txt; then
    echo "FAIL: CMakeLists.txt still lists shaders.qrc"
    failures=$((failures + 1))
else
    echo "ok:   CMakeLists.txt does not list shaders.qrc"
fi

if [ $failures -ne 0 ]; then
    echo "source.no_glsl_path: $failures check(s) failed"
    exit 1
fi
echo "source.no_glsl_path: all checks passed"
exit 0
