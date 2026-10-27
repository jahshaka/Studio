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

# sky.emits_what_it_shows — THE SKY LIGHT IS ONE GAIN ON THE SKY ITSELF (PHOTON-7 stage 10-3, R5): with no Sky Light
# the sky lights the scene (gain 1), and at every gain the sky and its light move together; R5b's dome and backdrop
# arms (the two author tools). Mirror-linked: the
# document's Sky Light through the mirror is the product's path. The file header carries the laws.
add_executable(test_sky_emits test_sky_emits.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_sky_emits PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_sky_emits PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_sky_emits PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_sky_emits PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_sky_emits JahshakaEngineMedia)
jah_gpu_row(sky.emits_what_it_shows CLASS engine COMMAND test_sky_emits
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.emits_what_it_shows PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(sky.emits_what_it_shows PROPERTIES JAHSHAKA_TEST_FRAMES 52)
