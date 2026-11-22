# MEASURING ARM (SCRIPT-IN-ENGINE-1, the lead's last item: SCRIPT OR C++, DECIDED BY MEASUREMENT): the C++ twins of
# the measured scripts, built as executables and NEVER registered as rows; the whole directory is reverted by the next
# commit. The scripts (measure_calls.js, measure_pixels.js) run by bin/jahengine from here.
foreach(_m measure_odd_size measure_calls)
    add_executable(${_m}_cpp ${_m}.cpp)
    set_target_properties(${_m}_cpp PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${_m}_cpp PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${_m}_cpp PRIVATE JahshakaEngine)
    add_dependencies(${_m}_cpp JahshakaEngineMedia)
endforeach()
