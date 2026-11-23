# Thumbnails through the engine (plan step 9): EngineThumbnailRenderer renders a
# document node into an offscreen View on the main thread. Output goes to bin/ so
# app/content/primitives/sphere.obj (relative to the executable) resolves.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# Shader-asset thumbnails (VISUAL_PARITY_SPEC item 5): MaterialReader::
# parseShaderAsPbr turns a stored graph definition into the evaluator's
# PbrMaterial and EngineThumbnailRenderer renders it on the preview sphere.
# Links the REAL Database (throwaway SQLite file) plus materialreader.cpp's
# link closure — the materials evaluator slice (same set the materialpreview
# suite compiles) with tests/shadergraph/test_stubs.cpp standing in for
# texturemanager.cpp. Binary in bin/ so app/content/primitives/sphere.obj
# resolves next to the executable; its OWN working directory so the throwaway
# database and the ogre log never land in the shared bin/.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Multimedia Concurrent)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)

# THE ASSET FIXTURE (PREVIEWS-1 stage 0): a 1 m box centred on the origin, written here at configure so no binary
# mesh lives in the tree — 8 corners, one normal a face, six outward (counter-clockwise from outside) quads, and ONE
# PURE RED material (box.mtl, stage 2): the studio is NEUTRAL by construction (one code to all three channels,
# previewenvironment.cpp), so a red subject's pixel can never equal a studio pixel - thumbnails.coverage's lower bound
# rests on it. An Object row by `assets.import`; its tile is the ASSET preset's.
# (The particle fixture is made by script: scene.addParticles -> node.exportArchive({type: "particleSystem"}) ->
# assets.import.)
set(JAH_THUMB_BOX_OBJ "${CMAKE_CURRENT_BINARY_DIR}/fixtures/box.obj")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/box.mtl" "# box.mtl - written by tests/thumbnails/CMakeLists.txt (PREVIEWS-1 stage 2)
newmtl Red
Kd 1 0 0
Ka 0 0 0
Ks 0 0 0
")
file(WRITE "${JAH_THUMB_BOX_OBJ}" "# box.obj - written by tests/thumbnails/CMakeLists.txt (PREVIEWS-1)
mtllib box.mtl
o Box
v -0.5 -0.5 -0.5
v 0.5 -0.5 -0.5
v 0.5 0.5 -0.5
v -0.5 0.5 -0.5
v -0.5 -0.5 0.5
v 0.5 -0.5 0.5
v 0.5 0.5 0.5
v -0.5 0.5 0.5
vn 0 0 -1
vn 0 0 1
vn -1 0 0
vn 1 0 0
vn 0 -1 0
vn 0 1 0
usemtl Red
f 1//1 4//1 3//1 2//1
f 5//2 6//2 7//2 8//2
f 1//3 5//3 8//3 4//3
f 2//4 3//4 7//4 6//4
f 1//5 2//5 6//5 5//5
f 4//6 8//6 7//6 3//6
")

# thumbnails.sweep (archived: archive/tests/thumbnails) — the REPAIR SWEEP's contract (THUMBS-1 fix round F1/F2/F6):
# it stops when asked (a yield delivers the window's close), it decides from
# (guid, type, has-a-thumbnail) rather than from every blob in the catalog, and
# it tells "nothing to draw" apart from "broken". Real Database on a throwaway
# file, a NULL engine (what a headless session hands it), no window: the
# assertions are about control flow and bookkeeping, never about pixels.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Multimedia Concurrent)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/sweep-run)

# thumbnails.studio_env — THE ONE STUDIO ENVIRONMENT, measured
# (MATPREVIEW-ENV-1, owner review R9). One binary because the assertion that
# matters most is an agreement between two surfaces: the Materials dock's
# preview scene and the shared thumbnail renderer, lit by the same generated
# environment, graded at the same derived exposure.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_preview_environment test_preview_environment.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginepreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/previewenvironment.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginethumbnailrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginematerialpreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp)
set_target_properties(test_preview_environment PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_preview_environment PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_preview_environment PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_preview_environment PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_preview_environment JahshakaEngineMedia)
# thumbnails.coverage — PREVIEWS-1 stage 2: A TILE SHOWS ITS SUBJECT (previews.coverage against the EMPTY studio at the
# same view: the material ball = its projected disc by a closed form, the box and its Mesh row = the front face's
# silhouette, the tetra's Mesh > 0), two definitions are two pictures by sha after one renderAll (RED at stage 0's base:
# 797bd607dbba99df twice), a Mesh whose bake file is absent fails by name and changes nothing. The derivations are the
# script's header. The app on the card, a fresh WARM home; the tetra written here at configure (no binary mesh in the
# tree). Its own row: a builder's, priced solo, never SHIP.
set(_tcHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-thumbnails_coverage)
set(JAH_THUMB_TETRA_OBJ "${CMAKE_CURRENT_BINARY_DIR}/fixtures/tetra.obj")
file(WRITE "${JAH_THUMB_TETRA_OBJ}" "# tetra.obj - written by tests/thumbnails/CMakeLists.txt (PREVIEWS-1 stage 2)
o Tetra
v 0 0.6 0
v -0.5 -0.3 0.4
v 0.5 -0.3 0.4
v 0 -0.3 -0.55
f 1 2 3
f 1 3 4
f 1 4 2
f 2 4 3
")
set(JAH_THUMB_PNG "${CMAKE_SOURCE_DIR}/tests/scripting/fixtures/implane_wide.png")
set(JAH_THUMB_OFFLINE "${_tcHome}/offline-store")
set(JAH_THUMB_OUT "${_tcHome}/out")
configure_file(thumbnails_coverage.js.in ${CMAKE_CURRENT_BINARY_DIR}/thumbnails_coverage.js @ONLY)
file(MAKE_DIRECTORY ${_tcHome}/run)
jah_host_args(thumbnails.coverage _tcHost)
# FRAMES 121: measured 2026-11-22 (pv2-thumbnails.coverage-frames, Ogre::Root::_fireFrameEnded counted over the row's
# app process at 64f68b889).
jah_gpu_row(thumbnails.coverage WARM_HOME "${_tcHome}" CLASS app TIER low NEEDS NONE FRAMES 121
         NEEDS_WHY "the tile's studio takes no tier feature (no GI, no post: secondarysurfacetonemap.h): subject coverage against the empty studio"
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/thumbnails_coverage.js ${_tcHost}
         WORKING_DIRECTORY ${_tcHome}/run)
set_tests_properties(thumbnails.coverage PROPERTIES
    ENVIRONMENT "HOME=${_tcHome};JAHSHAKA_DATA_ROOT=${_tcHome}/.local/share/Jahshaka"
    LABELS "mirror"
    TIMEOUT 60)
jah_fresh_home_fixture(thumbnails.coverage.fresh_home thumbnailsCoverageHome "${_tcHome}" WARM
    ARGS --mkdir "${JAH_THUMB_OFFLINE}" --mkdir "${JAH_THUMB_OUT}" LABELS "mirror")
set_property(TEST thumbnails.coverage APPEND PROPERTY FIXTURES_REQUIRED thumbnailsCoverageHome)

jah_gpu_row(thumbnails.studio_env SHIP CLASS engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_preview_environment
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(thumbnails.studio_env PROPERTIES JAHSHAKA_TEST_FRAMES 64)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(thumbnails.studio_env PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "mirror")

# THE CATALOGS' ACCEPTANCE — NOT A ROW (the lead, 2026-11-22: CODING_RULES § 5, no demo scene in any test or lane proof;
# the shipped samples are audited at the milestones only). PREVIEWS-1 stage 2's acceptance ("each shipped sample catalog
# reads 0 missing / 0 stale after ONE renderAll") is MEASURED by this script, configured here with the nine scenes/*.zip
# and run ONCE at the feature end by hand on the lane's rig (its command and its kept output: the script's header). Its
# registered proof of the same law on our own fixtures is thumbnails.library_complete.
file(GLOB _ccCatalogs LIST_DIRECTORIES false "${CMAKE_SOURCE_DIR}/scenes/*.zip")
list(SORT _ccCatalogs)
set(JAH_CATALOGS_JS "")
foreach(_c IN LISTS _ccCatalogs)
    string(APPEND JAH_CATALOGS_JS "\"${_c}\", ")
endforeach()
configure_file(catalogs_complete.js.in ${CMAKE_CURRENT_BINARY_DIR}/catalogs_complete.js @ONLY)

# thumbnails.library_complete — PREVIEWS-1 stage 2's ACCEPTANCE ON OUR OWN FIXTURE: a library reads 0 missing / 0 stale
# after ONE previews.renderAll at the project's scope and the library's; when a second project opens exactly the first
# project's pinned rows read stale (the open project's pin is in the key) and one more renderAll clears them. The library
# is tests/support/libraryfixture's, generated into the fresh WARM home's data root by test_library_statements
# --generate (the setup's THEN), plus a material made by materials.create and an emitter (printed). The app on
# the card. A builder's row, priced solo, never SHIP.
set(_lcHome ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-thumbnails_library_complete)
set(_lcRoot ${_lcHome}/.local/share/Jahshaka)
set(JAH_LC_OUT "${_lcHome}/out")
configure_file(library_complete.js.in ${CMAKE_CURRENT_BINARY_DIR}/library_complete.js @ONLY)
file(MAKE_DIRECTORY ${_lcHome}/run)
jah_host_args(thumbnails.library_complete _lcHost)
# FRAMES 169: measured 2026-11-22 (Ogre::Root::_fireFrameEnded counted over the row's app process by the preload meter
# spikes/previews-1/stage2/coverage/framecount-meter.c, at 9342d3786 + this row).
jah_gpu_row(thumbnails.library_complete WARM_HOME "${_lcHome}" CLASS app TIER low NEEDS NONE FRAMES 169
         NEEDS_WHY "the tile's studio takes no tier feature (no GI, no post: secondarysurfacetonemap.h): the library's state after one sweep"
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/library_complete.js ${_lcHost}
         WORKING_DIRECTORY ${_lcHome}/run)
set_tests_properties(thumbnails.library_complete PROPERTIES
    ENVIRONMENT "HOME=${_lcHome};JAHSHAKA_DATA_ROOT=${_lcRoot}"
    LABELS "mirror"
    TIMEOUT 60)
# THE SETUP: the fresh WARM home, THEN the generated library grown into its data root (the generator library.scale uses).
jah_fresh_home_fixture(thumbnails.library_complete.fresh_home thumbnailsLibraryHome "${_lcHome}" WARM
    ARGS --mkdir "${JAH_LC_OUT}"
    THEN $<TARGET_FILE:test_library_statements> --generate ${_lcRoot}/JahLibrary.db ${_lcRoot}/AssetStore 100 2
    LABELS "mirror")
set_property(TEST thumbnails.library_complete.fresh_home APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(TEST thumbnails.library_complete APPEND PROPERTY FIXTURES_REQUIRED thumbnailsLibraryHome)
