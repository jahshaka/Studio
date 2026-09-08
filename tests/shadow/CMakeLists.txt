# SHADOW TOOLING phase 0 — the isolated spike (SPECS/SHADOW_TOOLING_SPEC.md §9).
#
# Unlike every other engine suite this one includes EnginePrivate.h and links
# Ogre directly: the question it answers ("can static and dynamic shadow maps
# share one atlas if the clear becomes per-map quads?") is about compositor
# mechanics that have no public verb yet, and phase 0's whole purpose is to
# answer it before a verb is designed around it.
add_executable(test_shadow_spike test_shadow_spike.cpp)
set_target_properties(test_shadow_spike PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_shadow_spike PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_include_directories(test_shadow_spike PRIVATE
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
target_link_libraries(test_shadow_spike PRIVATE JahshakaEngine OgreNext)
add_dependencies(test_shadow_spike JahshakaEngineMedia)

# Only the arithmetic runs in the gate by default; the device-side modes are the
# phase-0 evidence and are driven by hand (and by the phase-1/2 suites that
# replace them).
add_test(NAME shadow.layout COMMAND test_shadow_spike layout
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadow.layout PROPERTIES TIMEOUT 120 LABELS "document")
jah_no_display(shadow.layout)
