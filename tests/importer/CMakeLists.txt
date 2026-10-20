# GLB importer regression suite (importer fix phases 0-2): embedded-texture
# extension correctness + real-PbrMaterial import through the REAL path
# (AssetHelper::extractTexturesAndMaterialFromMesh), tangent pass-through,
# tick->second animation extraction (SKELETAL_PLAYBACK_SPEC S1/S2/S5),
# relative skeletal sources, engine tolerant texture reads, and double-import
# transform stability. Engine sections need a reachable DISPLAY (Vulkan),
# platform stays offscreen.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)

# PLY + STL: the two mesh formats added to irisgl/CMakeLists.txt's assimp
# importer allowlist. The suite is a build-configuration gate as much as a
# format one — ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT is OFF, so dropping either
# option compiles the importer out and the extension fails at ReadFile,
# silently, at runtime. Drives the whole chain: raw assimp -> the canonical
# preset -> MeshNode::loadAsSceneFragment -> AssetHelper -> the ONE import
# pipeline (AssetImportService, the assets.importFile path) -> the engine
# thumbnail, with pixels. Needs the engine, so a reachable DISPLAY.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)
add_executable(test_importer_meshformats test_importer_meshformats.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginethumbnailrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginepreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/previewenvironment.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
    # the member stamp: the import spine clears it for a User-intent import (IMPORT-INTENT-1)
    ${CMAKE_SOURCE_DIR}/src/services/memberstamp.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectmembership.cpp
    # IMPORT-1: the service's reimport drops MeshBakeStore's caches and keys
    # the bake on the asset's import settings.
    ${CMAKE_SOURCE_DIR}/src/services/meshbakestore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/primitiveassets.cpp
    # The import record (SESSION_LOG_SPEC §5) is emitted from the one pipeline
    # entry point; JahLog is a leaf service (Qt + iris::Logger, nothing else).
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimporters.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    # database.cpp's link closure (same set tests/importasync links)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    # the project's VR settings table: the writer/reader are generated from it (VR-WORLD-1)
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)
set_target_properties(test_importer_meshformats PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_meshformats PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_meshformats PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_importer_meshformats PRIVATE IrisGL JahshakaEngine
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
jah_links_assimp(test_importer_meshformats)
add_dependencies(test_importer_meshformats JahshakaEngineMedia)
jah_gpu_row(importer.meshformats SHIP CLASS engine COMMAND test_importer_meshformats
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.meshformats PROPERTIES JAHSHAKA_TEST_FRAMES 6)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(importer.meshformats PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "importer")

# Import SECURITY suite (deep audit 2026-09 List A item 1): texture-path and
# mtllib containment through the ONE pipeline, IES bounds, the per-type source
# size cap, and the .blend extension-list correction. No engine — the whole
# point is the pipeline's handling of hostile FILE CONTENT, which happens long
# before anything renders.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)

# ANIMATION-ASSET suite (ModelTypes::Animation): the structural sniff, the
# clip reader (clip table + rig signature + pose strip) and the three import
# outcomes — clip file, model file, refusal. No engine, no display: the whole
# subject is file CONTENT and catalog rows. The binary-FBX cases are
# synthesised in the test (the walker reads record headers only), so no real
# character file has to ship as a fixture; the animation-only GLB, the rigged
# control and the .bvh are the avatar module's committed fixtures.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)

# The leak lane (scripts/sanitize.sh lsan): assimp scenes and the importer's own
# buffers. Only the two suites that do NOT link the engine — importer.glb (archived: archive/tests/importer) and
# importer.meshformats bring a Vulkan device up, which is what the lane's
# membership rule excludes.

# MATERIAL-IMPORT suite (the GLB material defects of 2026-09-08): the glTF
# workflow shapes — KHR_materials_pbrSpecularGlossiness, no PBR block at all,
# a present metallic-roughness block, KHR_materials_unlit — through the REAL
# import path, the emissiveIntensity round trip through the asset blob, and
# PIXELS (the imported spec-gloss material against the full-metal reading it
# used to get). Renders, so it needs the engine and a reachable DISPLAY.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)

# TEXTURE-GUID suite (the owner's "GLB imports come in without textures",
# 2026-09-09): the SAVE + REOPEN half of an imported model. The textures are
# right after import and were lost on the way back in, because one stored
# object backs two assets (the .glb Object's role='texture' row and the member
# Texture's role='source' row) and the writer's tie-break did not say which.
# Runs the REAL import pipeline into a throwaway store/db, saves a material the
# way a scene save does, reopens it, and drives the reader-side repair for
# scenes already saved wrong. No engine, no display, no GPU.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)

# importer.extent (archived: archive/tests/importer) — the IMPORT SETTINGS and the measurement
# (SPECS/IMPORT_DIALOG_SPEC.md §9). The three unit_cube fixtures pin what a
# FILE declares, the settings cases pin what a USER decides over it (unit
# override, uniform scale, rotation, origin), and the skinned section pins the
# one risk the whole design rests on: under a uniform k a rig's bind
# translations and clip position keys scale while every rotation is
# bit-identical. Real pipeline, real store, no engine rendering, no display.
#
# It replaces importer.fitsize: the size POLICY it gated (envelopes, an
# inferred fitScale, a per-instance multiply and the avatar double-normalisation
# order) retired with the import dialog.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)

# importer.scene (archived: archive/tests/importer) — THE FBX SCENE (BISTRO-IMPORT-1, SPECS/BISTRO-FIXES-1.md stages 1-3).
# One hand-written fixture of ours (fixtures/make_scene_fbx.py: a split specular pair,
# a cutout, a tinted glass with one missing map, a 2 m box under a pivot chain, a
# directional light, a camera and a Take that moves ONE node) through the real
# pipeline; the rows are counts: maps bound and missing, alpha modes, the root's
# name, the box's size, the light/camera nodes, one movable and the rest static.
# Headless document graph, no display.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)

