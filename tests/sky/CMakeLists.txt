# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
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

if(JAHSHAKA_ASAN)
    jahshaka_sky_test(test_sun_transmittance_asan)
    target_link_libraries(test_sun_transmittance_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_sun_transmittance_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_sun_transmittance_asan PRIVATE -fsanitize=address)
    jah_gpu_row(sky.sun_transmittance_asan SHIP CLASS engine COMMAND test_sun_transmittance_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Same reason as tests/engine, tests/ssr and tests/hdr: the only leaks LSan
    # finds here are libdbus-1 allocations reached from the Vulkan loader.
    set_tests_properties(sky.sun_transmittance_asan PROPERTIES TIMEOUT 60 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(sky.sun_transmittance_asan PROPERTIES JAHSHAKA_TEST_FRAMES 5)
endif()

# sky.env_layout (archived: archive/tests/sky) — THE REFLECTION CUBE'S LAYOUT (lane ENGINE-SMALL-B,
# ENVPROBE-LAYOUT-1). Under the Khronos validation layer, like
# shadow.cache_kinds (archived: archive/tests/shadow): the convolution used to leave the cubemap every PBR
# datablock samples in `ResourceLayout::Uav` (VK_IMAGE_LAYOUT_GENERAL) and
# nothing moved it — `VUID-vkCmdDraw-None-09600` on ten subresources of one
# image on an ordinary editor run. Any "Validation Error" fails the row
# whatever the exit code says, and the row's own assertions prove the mirror
# cube really samples the sky (so the layer half cannot pass by accident).

# sky.aerial_perspective (archived: archive/tests/sky) — THE AIR IS A MEDIUM UNDER THE REALISTIC SKY (lane
# FOG-ATMO-1). The file header carries the model, the instrument (two emissive
# levels read back in float) and the physics bar: T(2 km) within 0.01 of
# exp(-sigma d) at the atmosphere's one density.

# sky.atmosphere — THE PLANET'S ATMOSPHERE (SKY-ATMOSPHERE-1).
jahshaka_sky_test_src(test_atmosphere test_atmosphere.cpp)
target_link_libraries(test_atmosphere PRIVATE JahshakaEngine)
jah_gpu_row(sky.atmosphere SHIP CLASS engine COMMAND test_atmosphere
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.atmosphere PROPERTIES TIMEOUT 60 LABELS "engine")
set_tests_properties(sky.atmosphere PROPERTIES JAHSHAKA_TEST_FRAMES 231)
