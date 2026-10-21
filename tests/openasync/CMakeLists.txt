# open.responsive — the force-quit fix, measured.
#
# Spawns the REAL binary (the import.shutdown pattern), imports the Matcaps
# sample, opens it through the THREADED path while a main-thread heartbeat
# probe records the worst gap between its own ticks, and asserts:
#   * the open completes and really loaded the world,
#   * the app answered requests while it was in flight,
#   * no UI-thread gap beyond the budget (500 ms, or twice what the app's own
#     frame loop costs on this box right now — see kControlFactor in the suite),
#   * the synchronous project.open still opens the same world unchanged,
#   * EVERY shipped sample, opened through the synchronous verb, parses no
#     model on the UI thread (app.openStats(); OPEN-ASSIMP-1),
#   * quitting with an open in flight terminates the process cleanly,
#   * AND (OPEN-COVER-2b) THE STREAMING ARM: with the loading cover off — the
#     default — a create blocks no frame over 300 ms and draws no cover, the
#     Showroom 2 open's worst frame is printed against the same 300 and
#     asserted at 600 until PCC-BUDGET-1 lands, the watchdog reports no stall
#     at its default 2,000 ms, app.openTimings() carries the CREATE's ledger,
#     and project.createAsync hands back a guid before the world is installed,
#   * AND (CREATE-GAP-1) THE FULL LIBRARY: a create over a 40-project desktop
#     stays inside the same budget, rebuilds no grid, leaves one tile per
#     project, and its ledger carries the close it began with — and after those
#     40+ creates in one process the Properties column is shown, mounted and owes
#     nothing (the create-loop SIGSEGV's column; ui.show_walk (archived: archive/tests/ui) is its mechanism).
# EVERY RUN STARTS FROM A FIXED LIBRARY: the suite deletes the home's library
# database, its lock, the asset store and the project folders before it spawns
# the app (the shader cache stays) — the home used to grow ~14 projects a run.
find_package(Qt6 REQUIRED COMPONENTS Core Network)

# ONE ROW, AT THE STAGE CLOSE (TESTING-SPEED-B3): open.responsive.timing (archived: archive/tests/open) below is the suite — every
# count, structure and ledger assertion AND the millisecond bars armed (JAHSHAKA_TIMING_BARS=1), on a quiet
# box under the GPU-timing lock. Its push-tier twin `open.responsive` (the same binary in a second home,
# the bars only printed: 147 s a merge) is deleted; the merge tier's openasync guard is open.frames.
# open.responsive.timing (archived: archive/tests/open) — the counts and the millisecond bars (stage-close; `quiet-box`: never a scoped gate).
set(_open_timing_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-open-timing)
file(MAKE_DIRECTORY ${_open_timing_home}/run)

# open.frames — THE THREADED OPEN DRIVES ITS OWN FRAMES (lane OPEN-FRAMES-1).
#
# The deterministic half of the async-open crash fix: an open driven by a pump
# that renders NOTHING (no editor.frame anywhere in the script) must still have
# the renderer driven at every slice boundary, and must complete. It also pins
# the two contracts the same lane added around it: closeProject drains an
# install in flight, and project.openState() is the same predicate
# project.openAsync refuses on.
#
# Engine-up — it asserts real frames were drawn — so it needs the rig's display.
configure_file(e2e_open_frames.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_frames.js @ONLY)
set(_frames_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-frames)
file(MAKE_DIRECTORY ${_frames_home}/run)
jah_gpu_row(open.frames SHIP CLASS app TIER epic NEEDS photon
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_frames.js
         WORKING_DIRECTORY ${_frames_home}/run)
# budget 40 s: the measurement (TESTING-SPEED-B3, the lead's verdict 2026-10-12): 539 frames, ~23-25 s with the WARM
# home it runs in (a cold home's startup build is 40 s of its own: that is not this row's shape).
# TIMEOUT 300: three opens of the dragon sample plus two archive imports, cold
# (the home is not wiped between runs, so only the first is a cold bake).
set_tests_properties(open.frames PROPERTIES
    ENVIRONMENT "HOME=${_frames_home};JAHSHAKA_DATA_ROOT=${_frames_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "openasync")
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(open.frames)   # spawns the app: see jah_tsan_blocked

if(TEST open.frames)
    jah_fresh_home_fixture(open.frames.fresh_home openFramesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-frames" WARM LABELS "openasync")
    set_property(TEST open.frames APPEND PROPERTY FIXTURES_REQUIRED openFramesFreshHome)
endif()

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "open.frames=539"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()

# open.streams_to_the_end — THE OPEN STREAMS UNDER ITS BUDGET TO THE END (OPEN-PATH-1 stage 1; the law and its derivation
# are the script's head). A project of 1,500 distinct small meshes (make_many_meshes.py: 150 separate triangles each,
# 450 vertices, under the bake's LOD/cluster floor so the import is the parse) opened by the threaded runner with the
# driver live: no sync builds more than its share, and the world arrives over >= ceil(V / (share + one mesh)) - 2
# frames. RED at the base (the environment prime's unbudgeted sync built 1,354 meshes in one; 1 frame to the landing).
# FRAMES 693: the B3 meter (spikes/testing-speed-b3/framecount) over one warm run on this tree.
set(_streams_meshes 1500)
set(_streams_triangles 150)
set(_streams_model ${CMAKE_CURRENT_BINARY_DIR}/many-meshes-${_streams_meshes}x${_streams_triangles}.obj)
if(NOT EXISTS ${_streams_model})
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_many_meshes.py
                            ${_streams_model} ${_streams_meshes} ${_streams_triangles}
                    RESULT_VARIABLE _streams_rc)
    if(NOT _streams_rc EQUAL 0)
        message(FATAL_ERROR "open.streams_to_the_end: make_many_meshes.py failed (${_streams_rc})")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/make_many_meshes.py)
set(OPEN_STREAMS_MODEL ${_streams_model})
set(OPEN_STREAMS_MESHES ${_streams_meshes})
set(OPEN_STREAMS_TRIANGLES ${_streams_triangles})
configure_file(e2e_open_streams.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_streams.js @ONLY)
set(_streams_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-streams)
file(MAKE_DIRECTORY ${_streams_home}/run)

# open.first_frame_work — THE WORK BEFORE AN OPEN'S FIRST WORLD FRAME IS OFF THE UI THREAD (OPEN-PATH-1 stage 2; the law
# is the script's head): the same generated project with 100 materials and 100 textures beside the model, opened by the
# threaded runner — the session registration's queries one per asset type, the plan / membership / blob a worker's,
# and the same session set and node count as the blocking open. RED at the base (no db:sessionRows; the blob and the
# plan the UI thread's). FRAMES 478: the B3 meter over one run on this tree.
set(_first_materials 100)
set(_first_model ${CMAKE_CURRENT_BINARY_DIR}/first-frame/many-meshes-${_streams_meshes}x${_streams_triangles}m${_first_materials}.obj)
if(NOT EXISTS ${_first_model})
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/first-frame)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/make_many_meshes.py
                            ${_first_model} ${_streams_meshes} ${_streams_triangles} ${_first_materials}
                    RESULT_VARIABLE _first_rc)
    if(NOT _first_rc EQUAL 0)
        message(FATAL_ERROR "open.first_frame_work: make_many_meshes.py failed (${_first_rc})")
    endif()
endif()
set(OPEN_FIRST_MODEL ${_first_model})
set(OPEN_FIRST_MESHES ${_streams_meshes})
set(OPEN_FIRST_MATERIALS ${_first_materials})
configure_file(e2e_open_first_frame.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_first_frame.js @ONLY)
set(_first_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-first-frame)
file(MAKE_DIRECTORY ${_first_home}/run)

jah_gpu_row(open.streams_to_the_end CLASS app TIER low NEEDS NONE TEST_WINDOW FRAMES 693
         COMMAND ${JAH_FRESH_HOME} --warm ${_streams_home} --
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_open_streams.js --script-live
         WORKING_DIRECTORY ${_streams_home}/run)
set_tests_properties(open.streams_to_the_end PROPERTIES
    ENVIRONMENT "HOME=${_streams_home};JAHSHAKA_DATA_ROOT=${_streams_home}/.local/share/Jahshaka"
    TIMEOUT 60 LABELS "openasync")
jah_ship_row(open.streams_to_the_end)   # THE SHIP SET: the open path's end-to-end row (OPEN-PATH-1; the lead's promotion at the merge)
