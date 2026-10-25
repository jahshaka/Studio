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
# A pipeline the engine builds lazily reaches the disk (PIPELINE-CACHE-1 fix round):
# three engine processes against one directory, the driver's own disk cache off so
# the pipeline layer is the only thing that can make the third one warm.

# The application-level contract: the verbs, --clear-shader-cache, and the
# owner's rule that the startup shader build happens behind the launch screen.
# Spawns the REAL binary into a scratch HOME (the import.shutdown pattern) and reads
# app.shaderCache() from its output. TWO ROWS (TESTING-SPEED-B3; one row of five launches before):
#   shadercache.app        three launches — cold, warm (the steady state's compiled == 0 is its bar:
#                          the first warm launch IS the steady state since WARMUPSET-2), cleared.
#   shadercache.app.churn (archived: archive/tests/shadercache)  `--churn`, stage-close, its own home: a seeding cold launch, then the save
#                          under churn (the SMOKE-ENGINE-1 crash: the pass-cache delta < 8 across 300 sky
#                          captures, the capacity >= 8192), unchanged.
# WHAT MOVED WITH THE SPLIT, ON RECORD (the read's F2, accepted by the lead 2026-10-12): the old run 3 loaded a cache
# that a WARM launch had saved (run 2's clean-quit save). The merge row no longer has that warm-save -> next-load
# round trip; shadercache.app.churn (archived: archive/tests/shadercache) covers it at the sprint close — its churn launch loads the cache its seeding cold
# launch saved at its clean quit.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_shader_cache_app test_shader_cache_app.cpp)
set_target_properties(test_shader_cache_app PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_shader_cache_app PRIVATE Qt6::Core)
target_compile_definitions(test_shader_cache_app PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shadercache.app SHIP CLASS app TIER document   COMMAND test_shader_cache_app
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# budget 60 s: the measurement — three launches, 976 frames: 42.8-60.8 s loaded at the tip (cold 33-35, warm 4.6, cleared 19).
set_tests_properties(shadercache.app PROPERTIES TIMEOUT 600 LABELS "shadercache")

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
# THE SEED, ITS OWN ROW (TOOLING-7 stage 2; the derivation in test_async_after_open.cpp's header): the startup build is
# not this row's subject, and a fresh tree (every ship tree) paid it inside the row — 64 s solo, over the minute. The
# seed row boots the SAME World Mode (the tier and features below) and quits, only when the seed is missing; the row
# requires it (a fixture chain: FIXTURES_SETUP + FIXTURES_REQUIRED + DEPENDS, so `-R shader.async_after_open` runs both).
jah_gpu_row(shader.async_after_open.seed SHIP CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND test_async_after_open --seed
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.async_after_open.seed PROPERTIES TIMEOUT 180 LABELS "shadercache"   # 6 x its solo 28.2 s
    FIXTURES_SETUP asyncAfterOpenSeed)
jah_gpu_row(shader.async_after_open SHIP CLASS app TIER epic NEEDS photon bloom ssao smaa planar COMMAND test_async_after_open
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.async_after_open PROPERTIES TIMEOUT 1200 LABELS "shadercache"
    FIXTURES_REQUIRED asyncAfterOpenSeed DEPENDS shader.async_after_open.seed)

# THE SHIPPED MICROCODE SEED (COLD-BOOT-1 S1; EngineConfig::shaderCacheSeedDir, <exe dir>/shadercache-seed): a build's
# own warm microcode layer, read only when a launch's cache holds none at this build's key, never written.
# shadercache.seed — the CONTRACT at the engine, every branch an engine cycle on real directories (test_shader_seed.cpp's
# header): off / made by scripts/shadercache/make-seed.sh / loaded (0 compiled) / unused / stale / corrupt / absent, the
# seed byte-identical after them all. 6 cycles x 3 frames.
add_executable(test_shader_seed test_shader_seed.cpp)
set_target_properties(test_shader_seed PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_shader_seed PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_shader_seed PRIVATE JahshakaEngine)
add_dependencies(test_shader_seed JahshakaEngineMedia)
jah_gpu_row(shadercache.seed CLASS engine FRAMES 18
         COLD "the seed and the cache are the subject — the suite states its own directories and attacks them"
         COMMAND test_shader_seed WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.seed PROPERTIES TIMEOUT 60 LABELS "shadercache")
# shadercache.seed_boot.twin + shadercache.seed_boot — THE ACCEPTANCE at the app (test_shader_seed_boot.cpp's header): a
# virgin data root and HOME, the startup gate cold (the twin, whose saved cache make-seed.sh turns into the seed) and
# seeded (glslang <= 5 % of the twin's count, the seed byte-identical after), the gate's wall and longest stall printed
# side by side. A FIXTURE PAIR: a cold boot is ~29 s and a seeded one ~23 s, one row holding both would sit at the cap.
# FRAMES: the B3 meter (spikes/testing-speed-b3/framecount) on the builder's first green run, 2026-10-25 — 446 / 445.
add_executable(test_shader_seed_boot test_shader_seed_boot.cpp)
set_target_properties(test_shader_seed_boot PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_shader_seed_boot PRIVATE Qt6::Core)
target_compile_definitions(test_shader_seed_boot PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
jah_gpu_row(shadercache.seed_boot.twin CLASS app TIER document FRAMES 446 COMMAND test_shader_seed_boot --twin
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.seed_boot.twin PROPERTIES TIMEOUT 120 LABELS "shadercache"
    FIXTURES_SETUP shaderSeedBoot)
jah_gpu_row(shadercache.seed_boot CLASS app TIER document FRAMES 445 COMMAND test_shader_seed_boot
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.seed_boot PROPERTIES TIMEOUT 120 LABELS "shadercache"
    FIXTURES_REQUIRED shaderSeedBoot DEPENDS shadercache.seed_boot.twin)

# shader.async_soak (archived: archive/tests/shader) (ASYNC-SHADERS-1): 50 applies while flying, a blocking render meeting a pending
# permutation, a scene switch mid-compile and a quit with compiles pending — validation layer on.
# STAGE-CLOSE (TESTING-SPEED-B3): the soak is the ship's; shader.async_after_open is the merge tier's guard of
# the background compile.

# THE SAME SUITE, THE OTHER ROUTE (SHADER_CACHE_AUDIT F2). fork 8282f6d70 (was 0016) makes
# Ogre's CompositorPassWarmUp usable at all — without it the very first
# View::warmUpShaders() SEGVs in ForwardPlusBase::getGridBuffer, which is the
# crash this patch exists to remove and which this run is the proof of. The
# route ships OFF (a second, unfixed upstream read-after-destroy bites on the second
# world opened in a real editor session — see the comment in OgreChain.cpp), so
# this twin is what keeps the code honest and measures what it buys: case 6
# compiles a permutation parked behind the camera here and zero in the default
# run above. Its own working directory: both runs write an Ogre log.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/warmup_pass")

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing. The two async rows
# re-counted 2026-10-20 by the same meter at TOOLING-7's split (the seed's boot-and-quit 463, the cold + warm launches
# 918 + 382).
foreach(_row_frames
        "shader.async_after_open=1300"
        "shader.async_after_open.seed=463"
        "shadercache.app=976"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
