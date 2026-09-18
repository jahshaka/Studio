# THE SUN'S TRANSMITTANCE (lane SKY-DENSITY-1) — headless, framework-free, links
# JahshakaEngine ONLY, same runtime requirements as tests/engine and tests/hdr
# (a reachable DISPLAY, a Vulkan driver or lavapipe). The file header carries the
# defect, the model and its reference.
function(jahshaka_sky_test target)
    jahshaka_sky_test_src(${target} test_sun_transmittance.cpp)
endfunction()

function(jahshaka_sky_test_src target src)
    add_executable(${target} ${src})
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

jahshaka_sky_test(test_sun_transmittance)
target_link_libraries(test_sun_transmittance PRIVATE JahshakaEngine)
add_test(NAME sky.sun_transmittance COMMAND test_sun_transmittance
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.sun_transmittance PROPERTIES TIMEOUT 180 LABELS "engine")

if(JAHSHAKA_ASAN)
    jahshaka_sky_test(test_sun_transmittance_asan)
    target_link_libraries(test_sun_transmittance_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_sun_transmittance_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_sun_transmittance_asan PRIVATE -fsanitize=address)
    add_test(NAME sky.sun_transmittance_asan COMMAND test_sun_transmittance_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine, tests/ssr and tests/hdr: the only leaks LSan
    # finds here are libdbus-1 allocations reached from the Vulkan loader.
    set_tests_properties(sky.sun_transmittance_asan PROPERTIES TIMEOUT 300 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
endif()

# sky.env_layout — THE REFLECTION CUBE'S LAYOUT (lane ENGINE-SMALL-B,
# ENVPROBE-LAYOUT-1). Under the Khronos validation layer, like
# shadow.cache_kinds: the convolution used to leave the cubemap every PBR
# datablock samples in `ResourceLayout::Uav` (VK_IMAGE_LAYOUT_GENERAL) and
# nothing moved it — `VUID-vkCmdDraw-None-09600` on ten subresources of one
# image on an ordinary editor run. Any "Validation Error" fails the row
# whatever the exit code says, and the row's own assertions prove the mirror
# cube really samples the sky (so the layer half cannot pass by accident).
jahshaka_sky_test_src(test_env_layout test_env_layout.cpp)
target_link_libraries(test_env_layout PRIVATE JahshakaEngine)
add_test(NAME sky.env_layout COMMAND test_env_layout
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.env_layout PROPERTIES
    TIMEOUT 180 LABELS "engine"
    ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
    FAIL_REGULAR_EXPRESSION "Validation Error")
