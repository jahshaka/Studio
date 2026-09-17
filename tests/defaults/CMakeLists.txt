find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# THE EDITOR'S DEFAULTS, where a default is a DECISION rather than a leftover
# (EXPOSURE-1, 2026-09-17; RENDER AUDIT A13 — "the tests pin the wrong thing":
# nothing pinned a new scene plus a plane, which is the exact shape of the
# owner's smoke symptom 5).
#
# defaults.exposure_plane — the exposure default in pixels, the one
# ExposureDesc that crosses the mirror, and the values an absent key falls back
# to. Real engine, real frames; the headline assertions are STOPS OF MOVEMENT IN
# THE PICTURE, which is the only unit the symptom was ever reported in.
add_executable(test_exposure_plane test_exposure_plane.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_exposure_plane PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_exposure_plane PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_exposure_plane PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_exposure_plane PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_exposure_plane JahshakaEngineMedia)
add_test(NAME defaults.exposure_plane COMMAND test_exposure_plane
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(defaults.exposure_plane PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")
