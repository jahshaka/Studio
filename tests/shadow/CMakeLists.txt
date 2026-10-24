

# CUTOUT-CASTER-1 — AN ALPHA-TESTED CASTER'S SHADOW IS ITS MAP, AND IT NEVER LOSES THE GPU.
# The fork's caster fix (HlmsPbs: the caster's root layout holds the datablock's reflection map,
# fork 5afd7a796): before it the cut-out's texture set spilled into the sampler binding and its
# shadow was solid, absent or an Xid 109 from process to process.
#   shadow.cutout_caster    — a 3 m plane, a 4 x 4 hole checker, under the template's sun: the
#                             shadow is the checker cell by cell (the analytic projection: a
#                             horizontal caster over the floor casts a translated copy), and a
#                             SCROLLING cut-out's shadow holes move with the scroll.
#   shadow.two_sided_caster (archived: archive/tests/shadow) — the opaque plane two-sided, upright and turned over: the opaque
#                             plane's shadow (area within 10 %).
# THE GATE ROWS RUN ONE PROCESS (STAGE-CLOSE-1 U3): the claim, read once, in every gate that reaches
# them. THE CHURN — "one process proves nothing about the next" — is the `.churn` twins': the same
# script in TEN processes (shadow_casters.sh's last argument), labels `stage-close` + `engine`, in the
# stage-close batch beside gpu.cutout_soak and in a scoped gate only through a shadow/caster change
# (gate-scope.py STAGE_CLOSE_SUBJECTS). 80 runs / 8.84 h in a week of gates, 0 red ever, before the split.
# THE TIMEOUTS ARE max(6x THE QUIET MEDIAN, THE SLOT-ERA WORST) (tests/CMakeLists.txt's policy); set here (before that
# statistic was decided) as 6x the run-log ARCHIVE's quietest ten-process
# runs: cutout 454 s, two-sided 427 s (45.4 / 42.7 s a process). The one-process gate rows: 6 x 45.4 -> 272, 6 x 42.7 ->
# 256. The ten-process .churn twins: 6 x 349 -> 2100 and 6 x 320 -> 1950 (the run log's quiet ten-process worst
# 349 / 320 s). Measured alone in the slot on 2026-10-09 at load 14-18 (NOT quiet): cutout 124 / 179 / 84 s, two-sided
# 144 / 80 / 129 s — ~34 s of each the shutdown waiting on the shader-cache write under the box's I/O load. In the
# first stage-close batch the churn twins took 1008 / 1159 s, over the old TIMEOUT 900: they passed only through the
# hidden +900 s every gpu row's TIMEOUT carried for the admission wait — deleted by TESTING-CLEANUP-2B (the wait left
# the clock: tests/cmake/vram_rows.cmake), so 2100 / 1950 is now the twins' whole bound.
jah_gpu_row(shadow.cutout_caster SHIP CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/shadow_casters.sh cutout
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/shadow_casters.js.in
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cutout_holes.png
                 ${CMAKE_CURRENT_BINARY_DIR}/shadow.cutout_caster 1
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# budget 60 s: the measurement (TESTING-SPEED-B3): one process, 1,552 frames, 43.8-52 s loaded (the 377 s price was the
# six-process era's: stale).
set_tests_properties(shadow.cutout_caster PROPERTIES LABELS "engine" TIMEOUT 60)
# gpu.cutout_soak.primitives / .tornado — a cut-out material must never lose the GPU (CUTOUT-CASTER-1): every cut-out path
# draws under the Khronos validation layer — plain cut-outs on a cube and a sphere and the live graph cut-out the hang was
# found on (.primitives), the Tornado sample (.tornado) — through the viewport and the offscreen screenshot path, with a
# pinned and a free shader clock; any "Validation Error" or a lost device is red.
# REBUILT TO ITS LAW (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s). The defect was a caster's texture set bound
# over a root layout one slot short — a binding the validation layer names on the FIRST draw of the path; the Xid it
# became took ~2 minutes of drawing to show, and the old soak drew 5,000 frames of each scene (282 s, 11,260 frames) to
# reproduce the hang itself. The law needs every path drawn under the layer, not the minutes: ONE SOAK PERIOD per scene
# — 250 frames on the pinned clock + a screenshot, 250 on the free clock + a screenshot (500 frames) — and the two
# scenes as two rows, each its own process on its own data root (the soak's one-process claim was its minutes).
# Measured once each (2026-10-20, solo, the card lock held): primitives 37.4 s / 1,020 frames, tornado 36.7 s / 1,002.
# TIMEOUT 6 x the solo run, rounded up.
set(_soak_timeout_primitives 60)
set(_soak_timeout_tornado 60)
foreach(_part primitives tornado)
    jah_gpu_row(gpu.cutout_soak.${_part} SHIP CLASS app TIER epic NEEDS photon bloom ssao smaa planar
             COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gpu_cutout_soak.sh
                     $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     ${CMAKE_CURRENT_SOURCE_DIR}/scripts/gpu_cutout_soak.js.in
                     ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/cutout_holes.png
                     ${CMAKE_SOURCE_DIR}/scenes/Tornado.zip
                     ${CMAKE_CURRENT_BINARY_DIR}/gpu.cutout_soak.${_part} ${_part}
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(gpu.cutout_soak.${_part} PROPERTIES
        LABELS "engine" TIMEOUT ${_soak_timeout_${_part}}
        ENVIRONMENT "VK_LOADER_LAYERS_ENABLE=VK_LAYER_KHRONOS_validation"
        PASS_REGULAR_EXPRESSION "gpu_cutout_soak: ${_part} PASS"
        FAIL_REGULAR_EXPRESSION "Validation Error;DEVICE WAS LOST;assert failed")
endforeach()

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "gpu.cutout_soak.primitives=1020"   # TEST-CUT-1: one soak period (500) + the boot and the authoring
        "gpu.cutout_soak.tornado=1002"      # one soak period (500) + the boot and the Tornado import
        "shadow.cutout_caster=1552"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()

