# The session log (SPECS/SESSION_LOG_SPEC.md).
#
# log.core — the sink itself: levels, categories, the precedence chain, the
# file lifecycle, rotation, the Qt funnel and the §8-R5 micro-benchmark. Links
# ONLY JahLog + SessionHeader + the version constants (the services.core shape):
# no MainWindow, no engine, no document, no display. It re-execs ITSELF for the
# scenarios that need a real session file, because JahLog::start() is
# once-per-process by design.
#
# IrisGL is linked for one symbol: iris::Logger, which JahLog forwards into
# (fork F5-A, absorb at the sink). Nothing else in the document library is
# touched, and no engine boots.
find_package(Qt6 REQUIRED COMPONENTS Core Gui)
# No display at all: the whole point of the services.core shape.

# log.events — the verbs and the event markers against the REAL binary
# (SESSION_LOG_SPEC §10 phase 2 gate). Two halves, both needed: the JS asserts
# everything readable through log.since/log.tail/log.counts, and the shell
# wrapper asserts the ARTIFACT — brackets, block order, the ogre sibling, the
# latest.log pointer and the close summary — which no verb can vouch for.
#
# Inherits the session display like the other e2e suites (the engine needs a
# reachable X display), and takes its OWN HOME so the library DB is fresh.
set(_le_home ${CMAKE_CURRENT_BINARY_DIR}/log-events-home)
file(MAKE_DIRECTORY ${_le_home}/run)

# log.engine — the engine half (SESSION_LOG_SPEC §10 phase 3 gate): the Ogre
# LogListener, the device rows, the per-session ogre sibling and the pump's
# dedupe surviving absorption. The ONE log suite that boots a real render
# system, so it needs a reachable display like every other engine suite; it
# then runs a SECOND time under --headless to prove the same block degrades
# cleanly on the NULL render system instead of crashing.
set(_lg_home ${CMAKE_CURRENT_BINARY_DIR}/log-engine-home)
file(MAKE_DIRECTORY ${_lg_home}/run)
# TIER epic NEEDS photon (TEST-NEEDS-1, the lead's lifecycle rule): its claim is the engine's startup
# path (the Ogre log listener, the device rows) — Photon on, as the product boots; TEST_WINDOW keeps
# the 1280x720 window TEST-TIER-1 measured it at.
jah_gpu_row(log.engine SHIP CLASS app TIER epic NEEDS photon TEST_WINDOW
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/log_engine.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/e2e_log_engine.js"
         WORKING_DIRECTORY ${_lg_home}/run)
set_tests_properties(log.engine PROPERTIES
    ENVIRONMENT "HOME=${_lg_home};JAHSHAKA_DATA_ROOT=${_lg_home}/.local/share/Jahshaka"
    TIMEOUT 300
    LABELS "log")

# log.perf — the periodic performance sampler (SESSION_LOG_SPEC §10 phase 4
# gate). TWO halves in one script: the LINE through --script, and the TIMER
# under --mcp-port, because a QTimer only fires while an event loop is turning
# and a --script run never turns one. An EPHEMERAL MCP port (0): this suite and
# the input-synthesis driver both used to name 8751, and only the other one's
# RUN_SERIAL kept them from colliding (TEST_GATE_AUDIT.md §4.1).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(JAH_LOGPERF_CURL curl)
    find_program(JAH_LOGPERF_JQ jq)
endif()

if(TEST log.engine)
    jah_fresh_home_fixture(log.engine.fresh_home logEngineFreshHome "${CMAKE_CURRENT_BINARY_DIR}/log-engine-home" WARM LABELS "log")
    set_property(TEST log.engine APPEND PROPERTY FIXTURES_REQUIRED logEngineFreshHome)
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST log.engine)
    set_property(TEST log.engine PROPERTY JAHSHAKA_TEST_FRAMES 616)
endif()
