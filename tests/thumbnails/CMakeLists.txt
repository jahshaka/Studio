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
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Sql Multimedia Concurrent)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)

# thumbnails.sweep — the REPAIR SWEEP's contract (THUMBS-1 fix round F1/F2/F6):
# it stops when asked (a yield delivers the window's close), it decides from
# (guid, type, has-a-thumbnail) rather than from every blob in the catalog, and
# it tells "nothing to draw" apart from "broken". Real Database on a throwaway
# file, a NULL engine (what a headless session hands it), no window: the
# assertions are about control flow and bookkeeping, never about pixels.
find_package(Qt6 REQUIRED COMPONENTS Gui Sql Widgets Multimedia Concurrent)
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
jah_gpu_row(thumbnails.studio_env SHIP CLASS engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_preview_environment
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(thumbnails.studio_env PROPERTIES JAHSHAKA_TEST_FRAMES 64)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(thumbnails.studio_env PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
