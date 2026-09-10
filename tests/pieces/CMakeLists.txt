# Generated shader pieces (HLMS_ADOPTION P5). Engine-only suites: they link
# JahshakaEngine and nothing else, like tests/engine, because what they test is
# the boundary verb and the backend mechanism underneath it — no Qt, no app.
#
# Every suite here needs a reachable X server and a Vulkan driver (the engine
# suites' rule: Ogre's XCB support object connects at plugin load) and renders
# through createOffscreenView + readPixels.

function(jahshaka_piece_test target source)
    add_executable(${target} ${source})
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${target} PRIVATE JahshakaEngine)
    add_dependencies(${target} JahshakaEngineMedia)
endfunction()

# The stage-5a SPIKE: hand-written pieces, no generator. Proves the mechanism
# the emitter would target — it compiles, it renders, the clock moves it, an
# edit is a new file, and a scene without a piece stays byte-identical.
jahshaka_piece_test(test_piece_spike test_piece_spike.cpp)
add_test(NAME pieces.spike
         COMMAND test_piece_spike "${CMAKE_CURRENT_BINARY_DIR}/pieces"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(pieces.spike PROPERTIES TIMEOUT 120 LABELS "pieces")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/pieces")

# shadergraph.emitter_parity — THE DIFFERENTIAL ORACLE. Needs BOTH halves: the
# graph/evaluator slice (Qt widgets, because node models carry live widgets) and
# the engine (to render what the emitter produced). Modelled on
# tests/materialpreview, which pairs the same two worlds.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent)
add_executable(test_emitter_parity test_emitter_parity.cpp
    ${CMAKE_SOURCE_DIR}/tests/shadergraph/test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pieceemitter.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(test_emitter_parity PROPERTIES
    AUTOMOC ON AUTOUIC ON AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_emitter_parity PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_include_directories(test_emitter_parity PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_emitter_parity PRIVATE IrisGL JahshakaEngine
    Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_dependencies(test_emitter_parity JahshakaEngineMedia)
add_test(NAME shadergraph.emitter_parity
         COMMAND test_emitter_parity "${CMAKE_CURRENT_BINARY_DIR}/pieces"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.emitter_parity PROPERTIES TIMEOUT 120 LABELS "pieces")

# shadergraph.emitter_pieces — naming, dedup, the piece-collision allow-list,
# the fallback reasons and the baker hand-off. Graph slice only: no engine, no
# display, seconds not minutes.
add_executable(test_emitter_pieces test_emitter_pieces.cpp
    ${CMAKE_SOURCE_DIR}/tests/shadergraph/test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pieceemitter.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(test_emitter_pieces PROPERTIES
    AUTOMOC ON AUTOUIC ON AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_emitter_pieces PRIVATE EFFECT_BUILD_AS_LIB)
target_include_directories(test_emitter_pieces PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_emitter_pieces PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_test(NAME shadergraph.emitter_pieces COMMAND test_emitter_pieces
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.emitter_pieces PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "pieces")

# pieces.mirror — the document -> mirror -> renderer hop for a graph material
# that carries a generated piece. Same source slice as tests/materialpreview
# (the preview scene owns a real SceneMirror), plus the emitter.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Sql Concurrent Multimedia)
add_executable(test_piece_mirror test_piece_mirror.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginematerialpreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginepreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/tests/shadergraph/test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pieceemitter.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
# bin/, like tests/materialpreview: the preview's primitive meshes are loaded
# from paths relative to the working directory (app/content), so a suite that
# runs anywhere else silently gets no subject to render.
set_target_properties(test_piece_mirror PROPERTIES
    AUTOMOC ON AUTOUIC ON AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_piece_mirror PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_include_directories(test_piece_mirror PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_piece_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL
    Qt6::Widgets Qt6::Sql Qt6::Concurrent Qt6::Multimedia)
add_dependencies(test_piece_mirror JahshakaEngineMedia)
add_test(NAME pieces.mirror COMMAND ${CMAKE_BINARY_DIR}/bin/test_piece_mirror
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(pieces.mirror PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "pieces")

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(shadergraph.emitter_pieces)
