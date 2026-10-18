# The persistent shader cache's container, attacked (SHADER_CACHE_SPEC.md §4.5).
#
# Engine-level, no Qt, no app: it drives Engine::create/saveShaderCache/
# shaderCacheStats directly and mangles the files on disk between cycles. The
# whole test is sequential Engine create/destroy in one process, which only
# works because of fork d014b064f (was 0002) (see test_engine_recreate).
#
# TIMEOUT is 6x the measured run with a 60 s floor, like every other row since
# TEST_GATE_AUDIT.md §3: the corruption cases restore a byte snapshot of the
# seeded cache before each attack instead of re-seeding with another engine
# cycle, which took the plain run from 36 s to ~25 s and the sanitised twin
# (which additionally runs --corruption-only) from 572 s to ~215 s.
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
jah_gpu_row(shadercache.container CLASS engine COLD "the cache is the subject — the suite states its own dir and attacks it" COMMAND test_shader_cache
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# TESTING-CLEANUP-2B item 7, re-based after read 4 (2026-10-10; the sample per tests/CMakeLists.txt's policy — the run
# log's SLOT-ERA quiet PASS runs, since 2026-10-09 18:00, taken as the gate runs the row): WARM, n=5 — gate
# records 49.2 / 48.3 / 70.1 / 52.3 / 87.7 s (10-10 04:20 - 12:37; load 7-25, 0-4 builds beside them); median 52.3 ->
# 6x = 320; slot-era worst 87.7 (a scoped gate, 4 builds, load 25.2) -> TIMEOUT 320 (was 720).
# budget 60 s: the measurement (TESTING-SPEED-B3, the lead's verdict 2026-10-12) — 11 cases, ~51 engine
# create/destroy cycles (the corruption cases: every attack on every file) are the subject; 52 s quiet, 47-77 s loaded.
set_tests_properties(shadercache.container PROPERTIES TIMEOUT 320 LABELS "shadercache")
# A pipeline the engine builds lazily reaches the disk (PIPELINE-CACHE-1 fix round):
# three engine processes against one directory, the driver's own disk cache off so
# the pipeline layer is the only thing that can make the third one warm.
jah_gpu_row(shadercache.engine_pipelines CLASS engine COLD "the cache is the subject — three processes on the suite's own dir" COMMAND test_shader_cache --engine-pipelines
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.engine_pipelines PROPERTIES TIMEOUT 300 LABELS "shadercache"
    ENVIRONMENT "__GL_SHADER_DISK_CACHE=0")

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
    jah_gpu_row(shadercache.container_asan CLASS engine COLD "the cache is the subject — the suite states its own dir and attacks it" COMMAND test_shader_cache_asan --corruption-only
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asan")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asan")
    # LeakSanitizer off for the same reason tests/engine turns it off: the only
    # leaks it finds are inside libdbus-1, reached from the Vulkan loader's
    # device-select layer. Use-after-free / overflow detection is the point.
    # A STAGE-CLOSE ROW (TESTING-CLEANUP-2B item 11; docs/TESTING_GATE.md §1d): minutes of one ASan process
    # (56 engine cycles, 18 of them re-seeds; the run log's stage-close runs 225 / 465 s), so it leaves the
    # MERGE and PUSH tiers for the stage-close batch like every such row, and rides a scoped gate only
    # through its own subject (tests/shadercache, or the cache container: gate-scope.py STAGE_CLOSE_SUBJECTS).
    # Its own attack label — a second spelling of "not in the merge tier" — is retired; the row
    # keeps `shadercache` (its family: shadercache.app, .warmup and .warmup_pass stay in the merge tier).
    set_tests_properties(shadercache.container_asan PROPERTIES
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}"
        TIMEOUT 1500 LABELS "shadercache")
endif()

# The application-level contract: the verbs, --clear-shader-cache, and the
# owner's rule that the startup shader build happens behind the launch screen.
# Spawns the REAL binary into a scratch HOME (the import.shutdown pattern) and reads
# app.shaderCache() from its output. TWO ROWS (TESTING-SPEED-B3; one row of five launches before):
#   shadercache.app        three launches — cold, warm (the steady state's compiled == 0 is its bar:
#                          the first warm launch IS the steady state since WARMUPSET-2), cleared.
#   shadercache.app.churn  `--churn`, stage-close, its own home: a seeding cold launch, then the save
#                          under churn (the SMOKE-ENGINE-1 crash: the pass-cache delta < 8 across 300 sky
#                          captures, the capacity >= 8192), unchanged.
# WHAT MOVED WITH THE SPLIT, ON RECORD (the read's F2, accepted by the lead 2026-10-12): the old run 3 loaded a cache
# that a WARM launch had saved (run 2's clean-quit save). The merge row no longer has that warm-save -> next-load
# round trip; shadercache.app.churn covers it at the sprint close — its churn launch loads the cache its seeding cold
# launch saved at its clean quit.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_shader_cache_app test_shader_cache_app.cpp)
set_target_properties(test_shader_cache_app PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_shader_cache_app PRIVATE Qt6::Core)
target_compile_definitions(test_shader_cache_app PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shadercache.app CLASS app TIER document   COMMAND test_shader_cache_app
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# budget 60 s: the measurement — three launches, 976 frames: 42.8-60.8 s loaded at the tip (cold 33-35, warm 4.6, cleared 19).
set_tests_properties(shadercache.app PROPERTIES TIMEOUT 600 LABELS "shadercache")
jah_gpu_row(shadercache.app.churn CLASS app TIER document   COMMAND test_shader_cache_app --churn
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.app.churn PROPERTIES TIMEOUT 600 LABELS "shadercache")

# shadercache.save_off_frame — THE SAVE LEAVES THE FRAME (FRAME-MONITOR-DOOR-1 stage 4): free-running (--script-live,
# JAH_FREE_RUNNING=1), COLD on a fresh data root then WARM on the same root (save_off_frame.sh): a compile burst, the
# watchdog's save waited for in frames — A GUARD OF COUNTS: the copy chunked (cold), no chunked message over K = 8
# entries, no frame overrun across the save, the writer's write arrived (the milliseconds: the .timing sibling). Before: one
# message of 22.7 ms cold / 12.5 ms warm (spikes/frame-monitor-door/stage4-measure.txt).
set(_sof_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-save_off_frame)
file(MAKE_DIRECTORY ${_sof_home}/run)
jah_gpu_row(shadercache.save_off_frame CLASS app TIER document FRAMES 640
         COMMAND sh ${JAH_FRESH_HOME} ${_sof_home} --
                 bash "${CMAKE_CURRENT_SOURCE_DIR}/save_off_frame.sh" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_save_off_frame.js"
         WORKING_DIRECTORY ${_sof_home}/run)
set_tests_properties(shadercache.save_off_frame PROPERTIES
    ENVIRONMENT "HOME=${_sof_home};JAHSHAKA_DATA_ROOT=${_sof_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "shadercache")
jah_tsan_blocked(shadercache.save_off_frame)   # TSAN-GCC15-THREAD-REGISTRY: the copies' hand-off is frame.monitor_pop's arm
# shadercache.save_off_frame.timing — the guard's milliseconds, a MEASURE row (the ship's, alone on the card): the same
# two launches, and every render-thread message of every save <= 2 ms, every chunk <= 1 ms (the read's item 6: a guard
# counts; 0.61-0.86 ms against 1.0 on a loaded box is a measurement).
set(_soft_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-save_off_frame_timing)
file(MAKE_DIRECTORY ${_soft_home}/run)
jah_gpu_exclusive_test(NAME shadercache.save_off_frame.timing CLASS app TIER document RUN_TIMEOUT 300
         COMMAND sh ${JAH_FRESH_HOME} ${_soft_home} --
                 bash "${CMAKE_CURRENT_SOURCE_DIR}/save_off_frame.sh" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_save_off_frame.js" --timing
         WORKING_DIRECTORY ${_soft_home}/run)
set_tests_properties(shadercache.save_off_frame.timing PROPERTIES
    ENVIRONMENT "HOME=${_soft_home};JAHSHAKA_DATA_ROOT=${_soft_home}/.local/share/Jahshaka"
    JAHSHAKA_TEST_FRAMES 640 LABELS "shadercache")
jah_tsan_blocked(shadercache.save_off_frame.timing)

# shader.live_compiles (SHADER-WARM-2): after the splash, nothing compiles on the UI
# thread outside a compile window — a create/open in flight or a compile dialog
# (src/services/livecompiles.h). A cold launch, a warm one and a warm one whose global-pass
# key moved (a Studio-only change), each creating a Basic and a World project and re-opening
# the first; every phase must count 0 live compiles.
add_executable(test_live_compiles test_live_compiles.cpp)
set_target_properties(test_live_compiles PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_live_compiles PRIVATE Qt6::Core)
target_compile_definitions(test_live_compiles PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shader.live_compiles CLASS app TIER epic NEEDS photon bloom ssao smaa planar COMMAND test_live_compiles
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.live_compiles PROPERTIES TIMEOUT 600 LABELS "shadercache")

# shader.async_after_open (ASYNC-SHADERS-1): after a project is open, a new material and a tier
# change compile in the BACKGROUND — 0 UI-thread compiles, the placeholder in the presented
# frames, the pending count up then back to 0, the settled picture the warm launch's.
# THE COLD LAUNCH STARTS FROM THE STARTUP SEED (TESTING-SPEED-B3, the lead's verdict: the subject is the open's
# background compile, not the startup build): test_async_after_open.cpp's header. Its seed lives beside the home
# (e2e-async-after-open-seed/), made by the row when missing or stale. budget 40 s: the measurement — 1,325 frames,
# 33.1 s (load 6) / 34.8-36 s (load 10) at the tip (75 s quiet before). KEPT IN THE MERGE TIER over the 30 s rule (the
# lead, 2026-10-12): the background-compile guard is reached only when the shader service changes, and it is guarded
# nowhere else in the merge tier.
add_executable(test_async_after_open test_async_after_open.cpp)
set_target_properties(test_async_after_open PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_async_after_open PRIVATE Qt6::Core Qt6::Gui)
target_compile_definitions(test_async_after_open PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shader.async_after_open CLASS app TIER epic NEEDS photon bloom ssao smaa planar COMMAND test_async_after_open
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.async_after_open PROPERTIES TIMEOUT 1200 LABELS "shadercache")

# shader.sample_opens_quiet (ASYNC-SHADERS-1): every shipped sample opened on a cold cache with the
# background compiler on; after each open's window, 0 UI-thread compiles (the sentry). STAGE-CLOSE
# (TESTING-SPEED-B3): one cold launch opening every sample is the ship's property, with the owner's smoke.
add_executable(test_sample_opens_quiet test_sample_opens_quiet.cpp)
set_target_properties(test_sample_opens_quiet PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_sample_opens_quiet PRIVATE Qt6::Core)
target_compile_definitions(test_sample_opens_quiet PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shader.sample_opens_quiet CLASS app TIER document   COMMAND test_sample_opens_quiet
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.sample_opens_quiet PROPERTIES TIMEOUT 1800 LABELS "shadercache")

# shader.async_soak (ASYNC-SHADERS-1): 50 applies while flying, a blocking render meeting a pending
# permutation, a scene switch mid-compile and a quit with compiles pending — validation layer on.
# STAGE-CLOSE (TESTING-SPEED-B3): the soak is the ship's; shader.async_after_open is the merge tier's guard of
# the background compile.
add_executable(test_async_soak test_async_soak.cpp)
set_target_properties(test_async_soak PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_async_soak PRIVATE Qt6::Core)
target_compile_definitions(test_async_soak PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shader.async_soak CLASS app TIER epic NEEDS photon bloom ssao smaa planar COMMAND test_async_soak
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.async_soak PROPERTIES TIMEOUT 1200 LABELS "shadercache")

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
jah_gpu_row(shadercache.warmup CLASS engine COLD "counts what the warm-up compiles (shaderCacheOff in code)" COMMAND test_warm_up
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.warmup PROPERTIES TIMEOUT 120 LABELS "shadercache")

# THE SAME SUITE, THE OTHER ROUTE (SHADER_CACHE_AUDIT F2). fork 8282f6d70 (was 0016) makes
# Ogre's CompositorPassWarmUp usable at all — without it the very first
# View::warmUpShaders() SEGVs in ForwardPlusBase::getGridBuffer, which is the
# crash this patch exists to remove and which this run is the proof of. The
# route ships OFF (a second, unfixed upstream read-after-destroy bites on the second
# world opened in a real editor session — see the comment in OgreChain.cpp), so
# this twin is what keeps the code honest and measures what it buys: case 6
# compiles a permutation parked behind the camera here and zero in the default
# run above. Its own working directory: both runs write an Ogre log.
jah_gpu_row(shadercache.warmup_pass CLASS engine COLD "counts what the warm-up pass compiles (shaderCacheOff in code)" COMMAND test_warm_up
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/warmup_pass")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/warmup_pass")
set_tests_properties(shadercache.warmup_pass PROPERTIES
    ENVIRONMENT "JAHSHAKA_WARMUP_PASS=1"
    TIMEOUT 120 LABELS "shadercache")

# shadercache.fixture_warm — AN ENGINE FIXTURE BOOTS ON THE WARM SHARED CACHE, AND A CUSTOM PIECE NEVER COSTS IT
# (ENGINE-UPGRADE-1 stage 1, BOOT): three boots in one process through the fixture helper, the first with a material
# carrying a custom piece only it registers; the second and third compile NOTHING and load >= 80 shaders from the cache, and a save without the piece keeps its entries. Red on
# the base twice over (no cache for a fixture; a piece's entry dropped the whole cache). A law in counts; 9 frames.
add_executable(test_fixture_warm test_fixture_warm.cpp)
set_target_properties(test_fixture_warm PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_fixture_warm PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_fixture_warm PRIVATE JahshakaEngine)
add_dependencies(test_fixture_warm JahshakaEngineMedia)
jah_gpu_row(shadercache.fixture_warm CLASS engine FRAMES 9 COMMAND test_fixture_warm
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.fixture_warm PROPERTIES TIMEOUT 60 LABELS "shadercache")

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "shader.async_after_open=1325"
        "shader.async_soak=1738"
        "shader.live_compiles=3088"
        "shader.sample_opens_quiet=8366"
        "shadercache.app=976"
        "shadercache.app.churn=1320"
        "shadercache.container=136"
        "shadercache.container_asan=136"   # its plain twin's, an upper bound (the corruption cases only)
        "shadercache.engine_pipelines=180"
        "shadercache.warmup=18"
        "shadercache.warmup_pass=18"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
