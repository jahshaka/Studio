# The persistent shader cache's container, attacked (SHADER_CACHE_SPEC.md §4.5).
#
# Engine-level, no Qt, no app: it drives Engine::create/saveShaderCache/
# shaderCacheStats directly and mangles the files on disk between cycles. The
# whole test is sequential Engine create/destroy in one process, which only
# works because of ogre-patch 0002 (see test_engine_recreate).
#
# TIMEOUT is generous: every one of the corruption cases seeds a fresh cache
# with a real cold engine cycle first, and there are three attacks across every
# file in the directory.
add_executable(test_shader_cache test_shader_cache.cpp)
set_target_properties(test_shader_cache PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_shader_cache PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_shader_cache PRIVATE JahshakaEngine)
add_dependencies(test_shader_cache JahshakaEngineMedia)
add_test(NAME shadercache.container COMMAND test_shader_cache
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.container PROPERTIES TIMEOUT 900 LABELS "shadercache")

if(JAHSHAKA_ASAN)
    # The sanitised twin. "A corrupt file did not crash us" is not a claim you
    # can make from an uninstrumented run: the microcode loader's failure mode
    # is reading past a buffer it sized from a number in the file, and that is
    # precisely what a plain build can do silently. Links the sanitised copy of
    # the backend that tests/engine already builds.
    add_executable(test_shader_cache_asan test_shader_cache.cpp)
    set_target_properties(test_shader_cache_asan PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_shader_cache_asan PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_compile_options(test_shader_cache_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_shader_cache_asan PRIVATE -fsanitize=address)
    target_link_libraries(test_shader_cache_asan PRIVATE JahshakaEngineAsan)
    add_dependencies(test_shader_cache_asan JahshakaEngineMedia)
    add_test(NAME shadercache.container_asan COMMAND test_shader_cache_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asan")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asan")
    # LeakSanitizer off for the same reason tests/engine turns it off: the only
    # leaks it finds are inside libdbus-1, reached from the Vulkan loader's
    # device-select layer. Use-after-free / overflow detection is the point.
    set_tests_properties(shadercache.container_asan PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0"
        TIMEOUT 1800 LABELS "shadercache")
endif()

# The application-level contract: the verbs, --clear-shader-cache, and the
# owner's rule that the startup shader build happens behind the launch screen.
# Spawns the REAL binary three times into a scratch HOME (the import.shutdown /
# open.responsive pattern) and reads app.shaderCache() from its output.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_shader_cache_app test_shader_cache_app.cpp)
set_target_properties(test_shader_cache_app PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_shader_cache_app PRIVATE Qt6::Core)
target_compile_definitions(test_shader_cache_app PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
add_test(NAME shadercache.app COMMAND test_shader_cache_app
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.app PROPERTIES TIMEOUT 600 LABELS "shadercache")

# The per-scene PSO precache (SHADER_CACHE_SPEC.md §5), asserted by COUNTING
# COMPILES: after View::warmUpShaders() the first real frame must compile
# nothing. Its own process, like every engine suite (one Engine per process).
add_executable(test_warm_up test_warm_up.cpp)
set_target_properties(test_warm_up PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_warm_up PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_warm_up PRIVATE JahshakaEngine)
add_dependencies(test_warm_up JahshakaEngineMedia)
add_test(NAME shadercache.warmup COMMAND test_warm_up
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.warmup PROPERTIES TIMEOUT 600 LABELS "shadercache")

# THE SAME SUITE, THE OTHER ROUTE (SHADER_CACHE_AUDIT F2). ogre-patch 0016 makes
# Ogre's CompositorPassWarmUp usable at all — without it the very first
# View::warmUpShaders() SEGVs in ForwardPlusBase::getGridBuffer, which is the
# crash this patch exists to remove and which this run is the proof of. The
# route ships OFF (a second, unfixed upstream use-after-free bites on the second
# world opened in a real editor session — see the comment in OgreChain.cpp), so
# this twin is what keeps the code honest and measures what it buys: case 6
# compiles a permutation parked behind the camera here and zero in the default
# run above. Its own working directory: both runs write an Ogre log.
add_test(NAME shadercache.warmup_pass COMMAND test_warm_up
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/warmup_pass")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/warmup_pass")
set_tests_properties(shadercache.warmup_pass PROPERTIES
    ENVIRONMENT "JAHSHAKA_WARMUP_PASS=1"
    TIMEOUT 600 LABELS "shadercache")
