# The looks stage (SPECS/POST_LOOKS_SPEC.md §4) — headless, framework-free,
# links JahshakaEngine ONLY, exactly like tests/ssr and tests/planar beside it.
#
# It pixel-proves two different kinds of claim and both matter: that each look
# DOES something nameable at full amount, and that the whole stage is INVISIBLE
# when it is not asked for — an empty stack and a look at amount 0 are both
# byte-identical to the frame this engine rendered before the stage existed.
# The second kind is what every thumbnail, preview and pixel suite in the tree
# depends on.
#
# Same runtime requirements as tests/engine (a reachable DISPLAY, a Vulkan
# driver or lavapipe). The ASan copy earns its place the way tests/ssr's does:
# the stage adds two local textures and up to seven quad passes to the node
# definition, and the suite ends by tearing a view down with them live.
function(jahshaka_looks_test target)
    add_executable(${target} test_looks.cpp)
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
endfunction()

jahshaka_looks_test(test_looks)
target_link_libraries(test_looks PRIVATE JahshakaEngine)
add_test(NAME looks.engine COMMAND test_looks
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(looks.engine PROPERTIES TIMEOUT 120 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_looks_test(test_looks_asan)
    target_link_libraries(test_looks_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_looks_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_looks_asan PRIVATE -fsanitize=address)
    add_test(NAME looks.engine_asan COMMAND test_looks_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine and tests/ssr: the only leaks LSan finds are
    # libdbus-1 allocations reached from the Vulkan loader's device-select layer.
    set_tests_properties(looks.engine_asan PROPERTIES TIMEOUT 120 LABELS "engine"
                         ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
