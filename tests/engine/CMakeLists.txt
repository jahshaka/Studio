# Engine abstraction tests — headless, framework-free, link JahshakaEngine ONLY.
#
# Runtime requirements (both documented in spikes/headless-vulkan/README.md):
#  * A reachable X server (DISPLAY=:0, XWayland or Xvfb) even though every test is
#    offscreen: Ogre's VulkanXcbSupport connects at PLUGIN LOAD and throws if it
#    cannot. Not fixable from our side.
#  * A Vulkan driver. For a no-GPU CI box the same tests pass under lavapipe
#    (~30 s per test):  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
#  * Backend::Null is deliberately untested: the NULL RenderSystem cannot create
#    the RTT depth buffer and its readback ticket never sets TextureBox::data.
#
# The teardown defect this suite guards (a MeshPtr outliving Root) reproduced in
# only 3 of 9 plain runs, so a single green run proves little:
#     ctest --repeat until-fail:5 --output-on-failure
# JAHSHAKA_ASAN=ON adds a second, sanitised build of the suite (test_engine_asan)
# compiled straight from the backend sources, leaving the app's engine library alone.

function(jahshaka_engine_test target)
    add_executable(${target} test_engine.cpp ${ARGN})
    # Never Qt: the root sets CMAKE_AUTOMOC ON globally.
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        # Ogre's shared libraries live in the install prefix; nothing else puts it on the path.
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
    add_test(NAME ${target} COMMAND ${target}
             # The backend writes its log to the working directory.
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(${target} PROPERTIES TIMEOUT 600)
endfunction()

jahshaka_engine_test(test_engine)
target_link_libraries(test_engine PRIVATE JahshakaEngine)

# Create/destroy/create in one process — needs the OgreVulkanDevice.cpp patch (OGRE_PLATFORM_DEPS.md).
add_executable(test_engine_recreate test_engine_recreate.cpp)
set_target_properties(test_engine_recreate PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_recreate PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_recreate PRIVATE JahshakaEngine)
add_dependencies(test_engine_recreate JahshakaEngineMedia)
add_test(NAME test_engine_recreate COMMAND test_engine_recreate WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(test_engine_recreate PROPERTIES TIMEOUT 600)

# Decals (DECALS_SPEC.md): pixel-asserted projected textures, and the permanent
# home of the phase-0 spike's UV / -Y / pool findings.
add_executable(test_decals test_decals.cpp)
set_target_properties(test_decals PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_decals PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_decals PRIVATE JahshakaEngine)
add_dependencies(test_decals JahshakaEngineMedia)
add_test(NAME decals.engine COMMAND test_decals WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(decals.engine PROPERTIES TIMEOUT 600 LABELS "engine")

# The ON-SCREEN (swapchain) path — Linux/X11 only, and the only suite in the tree
# that binds a View to a native window. It never MAPS that window, so it is safe
# on a shared display; it still needs a real X server, like every other engine
# suite. Deep audit area 7 F1/F3.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_engine_window test_engine_window.cpp)
    set_target_properties(test_engine_window PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_engine_window PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(test_engine_window PRIVATE JahshakaEngine ${X11_LIBRARIES})
    add_dependencies(test_engine_window JahshakaEngineMedia)
    add_test(NAME engine.window COMMAND test_engine_window
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.window PROPERTIES TIMEOUT 600 LABELS "engine")
endif()

option(JAHSHAKA_ASAN "Also build the engine tests with AddressSanitizer" OFF)
if(JAHSHAKA_ASAN)
    # A private, sanitised copy of the backend: the app's JahshakaEngine stays clean.
    add_library(JahshakaEngineAsan STATIC ${JAHSHAKA_ENGINE_SOURCES})
    target_include_directories(JahshakaEngineAsan
        PUBLIC ${CMAKE_SOURCE_DIR}/irisgl/engine/include PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
    target_link_libraries(JahshakaEngineAsan PRIVATE OgreNext ${X11_LIBRARIES})
    # Same shader-cache fingerprint terms as the real library (irisgl/engine/
    # CMakeLists.txt) — OgreShaderCache.cpp will not compile without them.
    target_compile_definitions(JahshakaEngineAsan PRIVATE
        JAHSHAKA_ENGINE_BUILD_ID="${JAHSHAKA_ENGINE_BUILD_ID}"
        JAHSHAKA_OGRE_PATCH_SERIES="${JAHSHAKA_OGRE_PATCH_SERIES}")
    set_target_properties(JahshakaEngineAsan PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_options(JahshakaEngineAsan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(JahshakaEngineAsan PUBLIC -fsanitize=address)

    jahshaka_engine_test(test_engine_asan)
    target_link_libraries(test_engine_asan PRIVATE JahshakaEngineAsan)
    # LeakSanitizer is OFF: the only leaks it finds (6 allocations, ~4 KB) are inside
    # libdbus-1, reached from the Vulkan loader's Mesa device-select layer — not ours.
    # Use-after-free / overflow detection is the point here and stays on.
    set_tests_properties(test_engine_asan PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
    target_compile_options(test_engine_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_engine_asan PRIVATE -fsanitize=address)
endif()
