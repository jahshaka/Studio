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
# app.shaderCache() from its output. ONE ROW A LAUNCH (TESTING-SPEED-B3 made two of one row of five launches; HARNESS-4 split
# the first BY PROOF, 2026-10-29: three launches priced 48.36 s, over a row's 45-s law, timed out at 60.09 s on 8B's loaded
# check; URGENT #136, 2026-11-02: its cold + warm row, 32.08 s quiet, timed out at 60.11 s (10-30) and 60.02 s (11-02) on
# loaded master checks - a row at the cap with no margin, #96's class), a fixture chain on one home:
#   shadercache.app.cold      `--cold`: run 1 on a wiped data root (enabled, located, the startup build before the window,
#                          save writes files, clear keeps only the lock); its numbers kept for run 2 (cold-run.json) -
#                          FIXTURES_SETUP shadercacheAppCold;
#   shadercache.app.warm      `--warm`: run 2 on that home (the steady state's compiled == 0 is its bar, the first warm
#                          launch IS the steady state since WARMUPSET-2; its laws read run 1's numbers: a fraction of its
#                          compiles, the same fingerprint; it refuses a home with no cold run) - its home left WARM,
#                          FIXTURES_SETUP shadercacheAppWarm;
#   shadercache.app.cleared   `--cleared`: one launch with --clear-shader-cache on that warm home (it refuses a home with no
#                          cache) - cold again, no layer loaded;
#   shadercache.app.clean     `--clean`: the FIXTURES_CLEANUP of both fixtures, the home removed after every part that ran
#                          (no launch, no card; URGENT #133's rule: no DEPENDS chain).
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
# THE DRIVER's CACHE IS THE ROWS' OWN (ORPHAN-REDS-1 item 4, 2026-11-03; URGENT #152's class): run 1 boots a wiped data
# root (no VkPipelineCache) and run 3 clears it, so their ray pipelines come from the NVIDIA driver's per-executable disk
# cache alone - and the shared one (~/.cache/nvidia, every tree's and every concurrent row's "Jahshaka", the path the
# card's lock exports over the launcher's own XDG_CACHE_HOME) loses them (#152): .cold TIMED OUT at 60.01 s on the
# master checks at a9e64793c and 3f88e1e75 and ran 24.98 s solo with them warm (its startup gate 18.25 s). The three
# launches name <build>/tests/shadercache/app-driver-cache, written by these rows only, one at a time (the fixture
# chain); .cold is registered COLD, so the warm-up's cold-on boot runs it once a key, uncapped, and primes it. The
# variable rides the COMMAND (the cold-on runner reads no ENVIRONMENT property). The subject stays the APP's cache.
set(_scdriver ${CMAKE_CURRENT_BINARY_DIR}/app-driver-cache)
file(MAKE_DIRECTORY ${_scdriver})
jah_gpu_row(shadercache.app.cold SHIP CLASS app TIER document
         COLD "run 1 boots a wiped data root (the subject) - its driver cache is the rows' own, primed here"
         COMMAND env __GL_SHADER_DISK_CACHE_PATH=${_scdriver} $<TARGET_FILE:test_shader_cache_app> --cold
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.app.cold PROPERTIES TIMEOUT 60 LABELS "shadercache"
    FIXTURES_SETUP shadercacheAppCold)
jah_gpu_row(shadercache.app.warm SHIP CLASS app TIER document
         COMMAND env __GL_SHADER_DISK_CACHE_PATH=${_scdriver} $<TARGET_FILE:test_shader_cache_app> --warm
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.app.warm PROPERTIES TIMEOUT 60 LABELS "shadercache"
    FIXTURES_REQUIRED shadercacheAppCold FIXTURES_SETUP shadercacheAppWarm)
jah_gpu_row(shadercache.app.cleared SHIP CLASS app TIER document
         COMMAND env __GL_SHADER_DISK_CACHE_PATH=${_scdriver} $<TARGET_FILE:test_shader_cache_app> --cleared
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.app.cleared PROPERTIES TIMEOUT 60 LABELS "shadercache"
    FIXTURES_REQUIRED "shadercacheAppCold;shadercacheAppWarm")
add_test(NAME shadercache.app.clean COMMAND test_shader_cache_app --clean WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadercache.app.clean PROPERTIES TIMEOUT 60 LABELS "shadercache"
    FIXTURES_CLEANUP "shadercacheAppCold;shadercacheAppWarm")
jah_no_display(shadercache.app.clean)
jah_ship_row(shadercache.app.clean)   # THE SHIP SET (TEST-CUT-1): the cleanup of two ship rows' fixtures

# shader.async_after_open (ASYNC-SHADERS-1): after a project is open, a new material and a tier
# change compile in the BACKGROUND — 0 UI-thread compiles, the placeholder in the presented
# frames, the pending count up then back to 0, the settled picture the warm launch's.
# THE COLD LAUNCH STARTS FROM THE STARTUP SEED (TESTING-SPEED-B3, the lead's verdict: the subject is the open's
# background compile, not the startup build): test_async_after_open.cpp's header. Its seed lives beside the home
# (e2e-async-after-open-seed/), made by the row when missing or stale. budget 40 s: the measurement — 1,325 frames,
# 33.1 s (load 6) / 34.8-36 s (load 10) at the tip (75 s quiet before). KEPT IN THE MERGE TIER over the 30 s rule (the
# lead, 2026-10-12): the background-compile guard is reached only when the shader service changes, and it is guarded
# nowhere else in the merge tier.
# THE SEED's KEY IS THE PASS KEY's TERMS (TEST-STREAMLINE-1 point 3): the app's src/app/startuppasskey.cpp compiled in (one
# definition of the media digest and the key) and the build's JAH_STARTUP_PASS_CODE_ID header
add_executable(test_async_after_open test_async_after_open.cpp ${CMAKE_SOURCE_DIR}/src/app/startuppasskey.cpp)
target_include_directories(test_async_after_open PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_dependencies(test_async_after_open JahStartupPassId)
set_source_files_properties(test_async_after_open.cpp PROPERTIES OBJECT_DEPENDS "${JAH_STARTUP_PASS_HEADER}")
set_target_properties(test_async_after_open PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_async_after_open PRIVATE Qt6::Core Qt6::Gui)
target_compile_definitions(test_async_after_open PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
    JAHSHAKA_OGRE_INSTALL="${OGRE_NEXT_PREFIX}")
# THE SEED, ITS OWN ROW (TOOLING-7 stage 2; the derivation in test_async_after_open.cpp's header): the startup build is
# not this row's subject, and a fresh tree (every ship tree) paid it inside the row — 64 s solo, over the minute. The
# seed row boots the SAME World Mode (the tier and features below) and quits, only when the seed is missing; the row
# requires it (a fixture chain: FIXTURES_SETUP + FIXTURES_REQUIRED + DEPENDS, so `-R shader.async_after_open` runs both).
# COLD (URGENT #122, HARNESS-4): the seed is a cache keyed on the shaders (its tree key) that only its own boot may make -
# the row's law needs the open's tier change to compile, and the shared app cache holds tier low - so every shader
# change made this row a 64-s boot at the 60-s cap (8B's check: 60.04 s, the row behind it NOT RUN). Declared COLD, the
# warm-up's cold-on boot runs `--seed` once uncapped with this test tier (scripts/warm-app-cache.sh) and the row finds
# the seed this tree's (0.03 s).
jah_gpu_row(shader.async_after_open.seed CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COLD "the startup seed is a cache keyed on the shaders that only this row's own boot may make"
         COMMAND test_async_after_open --seed
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.async_after_open.seed PROPERTIES TIMEOUT 60 LABELS "shadercache"   # 6 x its solo 28.2 s
    FIXTURES_SETUP asyncAfterOpenSeed)
jah_gpu_row(shader.async_after_open CLASS app TIER epic NEEDS photon bloom ssao smaa planar COMMAND test_async_after_open
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shader.async_after_open PROPERTIES TIMEOUT 60 LABELS "shadercache"
    FIXTURES_REQUIRED asyncAfterOpenSeed DEPENDS shader.async_after_open.seed)

# shadercache.startup_pass_key (TEST-STREAMLINE-1 point 3, CPU: no card, no app) - the startup pass's key and the seeds'
# tree key are WHAT DECIDES THE PASS's BYTES, never the Studio commit: the media digest by content (no path, no time), the
# key's three terms and nothing else, the code id's generator (a listed source and the engine's full id move it, an
# unlisted file does not), the build's list (test_async_after_open.cpp keySelfTest).
add_test(NAME shadercache.startup_pass_key COMMAND test_async_after_open --key-self-test)
set_tests_properties(shadercache.startup_pass_key PROPERTIES TIMEOUT 60 LABELS "shadercache")
jah_no_display(shadercache.startup_pass_key)

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
# 918 + 382). The shadercache.app split (HARNESS-4, 2026-10-29) counted by an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded() rewritten per frame (the apps _Exit): cold 581 + warm 562, cleared 581 (the 976 of the
# whole row was a 2026-10-12 count of an older script set); cold 581 and warm 562 are their own rows since URGENT #136.
foreach(_row_frames
        "shader.async_after_open=1300"
        "shader.async_after_open.seed=463"
        "shadercache.app.cold=581"
        "shadercache.app.warm=562"
        "shadercache.app.cleared=581"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
