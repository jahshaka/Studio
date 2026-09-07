# Screen-space reflection test — headless, framework-free, links JahshakaEngine
# ONLY. Pixel-proves the SSR chain end to end (POST_CHAIN_SPEC.md §4.1 row "SSR"
# and §8 phase 6), including the one property no probe can have: the reflection
# follows a moving object within two frames.
#
# Same runtime requirements as tests/engine and tests/planar (a reachable
# DISPLAY, a Vulkan driver or lavapipe).
#
# The ASan copy is not decoration: the SSR shape adds five local textures, a
# second RTV and a prepass to the node definition, and the suite ends by
# destroying a view with that chain live — exactly the shape a sanitiser catches
# and a plain run does not.
function(jahshaka_ssr_test target)
    add_executable(${target} test_ssr.cpp)
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

jahshaka_ssr_test(test_ssr)
target_link_libraries(test_ssr PRIVATE JahshakaEngine)
add_test(NAME ssr.engine COMMAND test_ssr
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(ssr.engine PROPERTIES TIMEOUT 600 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_ssr_test(test_ssr_asan)
    target_link_libraries(test_ssr_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_ssr_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_ssr_asan PRIVATE -fsanitize=address)
    add_test(NAME ssr.engine_asan COMMAND test_ssr_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine: the only leaks LSan finds here are libdbus-1
    # allocations reached from the Vulkan loader's Mesa device-select layer.
    set_tests_properties(ssr.engine_asan PROPERTIES TIMEOUT 900 LABELS "engine"
                         ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
