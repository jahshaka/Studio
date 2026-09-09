# GLB importer regression suite (importer fix phases 0-2): embedded-texture
# extension correctness + real-PbrMaterial import through the REAL path
# (AssetHelper::extractTexturesAndMaterialFromMesh), tangent pass-through,
# tick->second animation extraction (SKELETAL_PLAYBACK_SPEC S1/S2/S5),
# relative skeletal sources, engine tolerant texture reads, and double-import
# transform stability. Engine sections need a reachable DISPLAY (Vulkan),
# platform stays offscreen.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)
add_executable(test_importer_glb test_importer_glb.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    # database.cpp's link closure (same as tests/assetmeta)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    # transform-panel echo regression (the double-import scale corruption)
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragspinbox.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp)
set_target_properties(test_importer_glb PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_glb PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_glb PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_importer_glb PRIVATE IrisGL JahshakaEngine
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_importer_glb)
add_dependencies(test_importer_glb JahshakaEngineMedia)
add_test(NAME importer.glb COMMAND test_importer_glb
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.glb PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 600 LABELS "importer")
jah_no_display(importer.glb)

# FBX importer regression suite (assimp-patches/0001): a skin cluster that
# references a control point no polygon uses must not abort the importer.
# Pure assimp + the canonical preset — no Qt, no display, no engine.
add_executable(test_importer_fbx test_importer_fbx.cpp)
set_target_properties(test_importer_fbx PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_importer_fbx PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include)
target_compile_definitions(test_importer_fbx PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_importer_fbx PRIVATE assimp)
add_test(NAME importer.fbx COMMAND test_importer_fbx
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.fbx PROPERTIES TIMEOUT 120 LABELS "importer")

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
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
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
    # database.cpp's link closure (same set tests/importasync links)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)
set_target_properties(test_importer_meshformats PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_meshformats PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_meshformats PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_importer_meshformats PRIVATE IrisGL JahshakaEngine
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
add_dependencies(test_importer_meshformats JahshakaEngineMedia)
add_test(NAME importer.meshformats COMMAND test_importer_meshformats
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.meshformats PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "importer")

# Import SECURITY suite (deep audit 2026-09 List A item 1): texture-path and
# mtllib containment through the ONE pipeline, IES bounds, the per-type source
# size cap, and the .blend extension-list correction. No engine — the whole
# point is the pipeline's handling of hostile FILE CONTENT, which happens long
# before anything renders.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)
add_executable(test_importer_security test_importer_security.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
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
    # database.cpp's link closure (same set tests/importasync links)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)
set_target_properties(test_importer_security PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_security PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_security PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_importer_security PRIVATE IrisGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_importer_security)
add_test(NAME importer.security COMMAND test_importer_security
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.security PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "importer")
jah_no_display(importer.security)

# ANIMATION-ASSET suite (ModelTypes::Animation): the structural sniff, the
# clip reader (clip table + rig signature + pose strip) and the three import
# outcomes — clip file, model file, refusal. No engine, no display: the whole
# subject is file CONTENT and catalog rows. The binary-FBX cases are
# synthesised in the test (the walker reads record headers only), so no real
# character file has to ship as a fixture; the animation-only GLB, the rigged
# control and the .bvh are the avatar module's committed fixtures.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)
add_executable(test_importer_animation test_importer_animation.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimporters.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
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
    # database.cpp's link closure (same set tests/importasync links)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)
set_target_properties(test_importer_animation PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_animation PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_animation PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_importer_animation PRIVATE IrisGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
# A document node is an engine node now (SCENEGRAPH_SPEC D2) — the headless
# engine, NULL render system, no display, no GPU. jah_no_display proves it.
jah_document_graph(test_importer_animation)
add_test(NAME importer.animation COMMAND test_importer_animation
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.animation PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "importer")
jah_no_display(importer.animation)

# The leak lane (scripts/sanitize.sh lsan): assimp scenes and the importer's own
# buffers. Only the two suites that do NOT link the engine — importer.glb and
# importer.meshformats bring a Vulkan device up, which is what the lane's
# membership rule excludes.
# importer.fbx is clean; importer.security is QUARANTINED (L3 — every
# imported asset leaks its AssetNodeObject and the mesh subtree under it).
jah_lsan_lane(importer.fbx)
jah_lsan_blocked(importer.security)

# MATERIAL-IMPORT suite (the GLB material defects of 2026-09-08): the glTF
# workflow shapes — KHR_materials_pbrSpecularGlossiness, no PBR block at all,
# a present metallic-roughness block, KHR_materials_unlit — through the REAL
# import path, the emissiveIntensity round trip through the asset blob, and
# PIXELS (the imported spec-gloss material against the full-metal reading it
# used to get). Renders, so it needs the engine and a reachable DISPLAY.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)
add_executable(test_importer_materials test_importer_materials.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginethumbnailrenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    # database.cpp's link closure (same as tests/importer's other suites)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_importer_materials PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_materials PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_materials PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_importer_materials PRIVATE IrisGL JahshakaEngine
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
add_dependencies(test_importer_materials JahshakaEngineMedia)
add_test(NAME importer.materials COMMAND test_importer_materials
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.materials PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "importer")

# TEXTURE-GUID suite (the owner's "GLB imports come in without textures",
# 2026-09-09): the SAVE + REOPEN half of an imported model. The textures are
# right after import and were lost on the way back in, because one stored
# object backs two assets (the .glb Object's role='texture' row and the member
# Texture's role='source' row) and the writer's tie-break did not say which.
# Runs the REAL import pipeline into a throwaway store/db, saves a material the
# way a scene save does, reopens it, and drives the reader-side repair for
# scenes already saved wrong. No engine, no display, no GPU.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)
add_executable(test_importer_texture_guids test_importer_texture_guids.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimporters.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    # database.cpp's link closure (same as tests/importer's other suites)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_importer_texture_guids PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_importer_texture_guids PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_texture_guids PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_importer_texture_guids PRIVATE IrisGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::Concurrent)
add_test(NAME importer.texture_guids COMMAND test_importer_texture_guids
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.texture_guids PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "importer")
jah_no_display(importer.texture_guids)

# FIT TO SIZE (services/fitsize.h) — the import-time size policy, owner report
# 2026-09-09 ("everything is coming in messed up in the asset module and the
# avatars"). The three unit_cube fixtures pin the MEASUREMENT (one cube
# declared m / cm / mm), the giant/tiny/plain rig2 fixtures pin the POLICY in
# both directions with a control, and section 5 pins the ORDER that keeps the
# Avatar module from normalizing an already-fitted character a second time —
# which is why this suite links avatarpreviewmodel.cpp. Real pipeline, real
# store, no engine rendering, no display.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)
add_executable(test_importer_fitsize test_importer_fitsize.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetimporter.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimporters.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    # the Avatar module's height rule — section 5 asserts it is a NO-OP on a
    # fitted character (the double-normalisation gate)
    ${CMAKE_SOURCE_DIR}/src/modules/avatar/avatarpreviewmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/avatar/avatarspace.cpp
    # database.cpp's link closure (same as tests/importer's other suites)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_importer_fitsize PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_importer_fitsize PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_fitsize PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_importer_fitsize PRIVATE IrisGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::Concurrent)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_importer_fitsize)
add_test(NAME importer.fitsize COMMAND test_importer_fitsize
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.fitsize PROPERTIES
                     ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "importer")
jah_no_display(importer.fitsize)
