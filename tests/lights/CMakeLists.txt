# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
# The LIGHTING_FIX acceptance suites — headless, framework-free, engine only.
# Same runtime requirements as tests/engine (a reachable DISPLAY, a Vulkan
# driver or lavapipe).
#
# TWO BINARIES, deliberately. lights.falloff measures the light's own geometry
# (attenuation range, spot cone) with an ambient of zero; lights.hygiene (archived: archive/tests/lights)
# measures the shadow atlas and the shader-compile counters, which need a warm
# cache and a scene that has already rendered. Neither wants the other's setup,
# and the compile counters are process-wide.

add_executable(test_light_falloff test_light_falloff.cpp)
set_target_properties(test_light_falloff PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_light_falloff PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_light_falloff PRIVATE JahshakaEngine)
add_dependencies(test_light_falloff JahshakaEngineMedia)
jah_gpu_row(lights.falloff SHIP CLASS engine COMMAND test_light_falloff
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.falloff PROPERTIES TIMEOUT 60 LABELS "engine")
set_tests_properties(lights.falloff PROPERTIES JAHSHAKA_TEST_FRAMES 54)


# lights.lamp_ray_origin — A LAMP's VISIBILITY RAY NEVER HITS THE SURFACE IT STARTS ON (GATHER-HANG-1 stage 3; the
# diagnosis spikes/black-specks/DIAGNOSIS.md): a two-triangle plane, a non-shadowing point light 0.3 m from it, no
# ambient, GI off; no pixel of the light's footprint reads exactly 0 with the ray tier on - an 80 m and a 200 m plane,
# the Atom and the stock draw path - its negative controls the ray tier off and the plane in 10 m quads. Red first on
# the base (24 specks at 80 m: spikes/gather-hang-1/stage3/); its non-zero control rays.originCut 0 at 200 m (> 0).
# Frames: 7 pictures x 8.
add_executable(test_lamp_ray_origin test_lamp_ray_origin.cpp)
set_target_properties(test_lamp_ray_origin PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_lamp_ray_origin PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_lamp_ray_origin PRIVATE JahshakaEngine)
add_dependencies(test_lamp_ray_origin JahshakaEngineMedia)
jah_gpu_row(lights.lamp_ray_origin CLASS engine COMMAND test_lamp_ray_origin
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(lights.lamp_ray_origin PROPERTIES TIMEOUT 60 LABELS "engine")
set_tests_properties(lights.lamp_ray_origin PROPERTIES JAHSHAKA_TEST_FRAMES 56)
