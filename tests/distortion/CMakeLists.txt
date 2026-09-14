# The distortion pass (SPECS/POST_LOOKS_SPEC.md §5) — headless, framework-free,
# links JahshakaEngine ONLY, exactly like tests/looks and tests/ssr beside it.
#
# The suite it most resembles is tests/looks, and for the same reason: half its
# assertions are that the feature DOES something and half are that it is
# invisible when it should be. The second half matters more here — a distortion
# object's texture is a displacement field, and drawn as colour anywhere (a
# thumbnail, a probe capture, a G-buffer) it is a defect with no error message.
# The passthrough negative is the assertion that protects the rest of the test
# tree from this feature.
#
# Same runtime requirements as tests/engine (a reachable DISPLAY, a Vulkan
# driver or lavapipe).
function(jahshaka_distortion_test target)
    add_executable(${target} test_distortion.cpp)
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

jahshaka_distortion_test(test_distortion)
target_link_libraries(test_distortion PRIVATE JahshakaEngine)
add_test(NAME distortion.engine COMMAND test_distortion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(distortion.engine PROPERTIES TIMEOUT 120 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_distortion_test(test_distortion_asan)
    target_link_libraries(test_distortion_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_distortion_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_distortion_asan PRIVATE -fsanitize=address)
    add_test(NAME distortion.engine_asan COMMAND test_distortion_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine and tests/ssr: the only leaks LSan finds are
    # libdbus-1 allocations reached from the Vulkan loader's device-select layer.
    set_tests_properties(distortion.engine_asan PROPERTIES TIMEOUT 120 LABELS "engine"

    # THE ASan TWIN BOOTS WITH THE RAY TIER OFF (PHOTON_SPEC §7 R1). MEASURED,
    # not assumed: on NVIDIA 595.84 `vkCreateDevice` returns
    # VK_ERROR_INITIALIZATION_FAILED when ogre-patch 0038's ray-query features
    # are enabled UNDER AddressSanitizer, on the very GPU where the same call
    # succeeds without it, and the validation layer reports nothing — the driver
    # simply refuses. JAHSHAKA_NO_RAY_QUERY makes patch 0038 inert, so this
    # suite runs on EXACTLY the device it had before the ray tier existed and
    # keeps testing what it was written to test. It is a driver limit, recorded
    # here rather than worked around: the non-ASan build of the same code
    # creates the device and traces correctly (gi.rayquery).
                         ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;JAHSHAKA_NO_RAY_QUERY=1")
endif()
