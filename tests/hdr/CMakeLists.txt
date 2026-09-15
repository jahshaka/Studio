# THE HDR AUTO-EXPOSURE'S STABILITY (lane HDR-1, ogre-patch 0042) — headless,
# framework-free, links JahshakaEngine ONLY, same runtime requirements as
# tests/engine and tests/ssr (a reachable DISPLAY, a Vulkan driver or lavapipe).
#
# It asserts on View::measuredExposureScale() — the number the tonemapper
# samples, read back off the chain's own 1x1 adaptation history — rather than on
# pixels, because the subject is the GRADE and a picture only shows it
# indirectly. See the file header for the defect and the measurements.
function(jahshaka_hdr_test target)
    add_executable(${target} test_hdr_drag.cpp)
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

jahshaka_hdr_test(test_hdr_drag)
target_link_libraries(test_hdr_drag PRIVATE JahshakaEngine)
add_test(NAME hdr.drag_stable COMMAND test_hdr_drag
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(hdr.drag_stable PROPERTIES TIMEOUT 180 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_hdr_test(test_hdr_drag_asan)
    target_link_libraries(test_hdr_drag_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_hdr_drag_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_hdr_drag_asan PRIVATE -fsanitize=address)
    add_test(NAME hdr.drag_stable_asan COMMAND test_hdr_drag_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine and tests/ssr: the only leaks LSan finds here
    # are libdbus-1 allocations reached from the Vulkan loader's device-select layer.
    set_tests_properties(hdr.drag_stable_asan PROPERTIES TIMEOUT 300 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()
