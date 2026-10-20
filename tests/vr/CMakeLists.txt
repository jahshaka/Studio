# THE VR SUITES (SPECS/VR_SPEC.md §6). (`vr.spike_1a`, phase 1a's external-device
# spike, was deleted by VR-START-1 with the route it proved: the engine no longer
# boots on a runtime-made device; `vr.session` (archived: archive/tests/vr) is the session's suite.)
#
# EVERYTHING BELOW THE FIRST GUARD IS OPTIONAL. Without libopenxr-dev, without
# the Vulkan render system, or on macOS, none of the RUNTIME suites is built and
# none is registered; without monado-service on the box the registered ones SKIP
# (exit 77). A box with no VR stack is a supported box.
#
# `vr.grab_maths` (archived: archive/tests/vr) (stage 1, immediately below) is the exception and is
# registered FIRST, before any guard: it is pure arithmetic with no runtime, no
# display and no GPU in it, so it runs everywhere — see its own note.

if(NOT UNIX OR APPLE)
    return()
endif()

find_package(OpenXR QUIET)
find_package(Vulkan QUIET)
if(NOT OpenXR_FOUND OR NOT Vulkan_FOUND OR NOT OGRE_NEXT_VULKAN_RS_LIB)
    message(STATUS "Jahshaka: the VR runtime suites NOT built — openxr=${OpenXR_FOUND} "
                   "vulkan=${Vulkan_FOUND} vulkanRs='${OGRE_NEXT_VULKAN_RS_LIB}'")
    return()
endif()

# The manifest is named EXPLICITLY and the runtime's name is asserted by the
# suites (JAH_VR_EXPECT_RUNTIME): the USER-level manifest (~/.config/openxr/1/) is WiVRn's and is
# rewritten whenever the owner plugs a headset in (VR_SPEC §2.6, §7 item 10).
set(JAH_MONADO_MANIFEST "/usr/share/openxr/1/openxr_monado.json"
    CACHE FILEPATH "OpenXR manifest of the simulated-HMD runtime the VR gate uses")

# THE VR RUNTIME ROWS RUN AT THE SHIP (TESTING-SPEED-B4; SPECS/TESTING_SPEED_ROWS.md, vr): every row
# below that starts a Monado runtime — vr.session (archived: archive/tests/vr), vr.gather_stereo (archived: archive/tests/vr), vr.no_picture_start (archived: archive/tests/vr), vr.mirror_live,
# vr.start_recovery (archived: archive/tests/vr), vr.start_press (archived: archive/tests/vr), vr.start_broken (archived: archive/tests/vr) (a broken manifest, but its wrapper still SKIPs
# without Monado: it moves with them), vr.input_session (archived: archive/tests/vr), vr.warmup (archived: archive/tests/vr), vr.warmup_control (archived: archive/tests/vr), vr.eye_grade (archived: archive/tests/vr) and
# the two pools pool.vr_session (archived: archive/tests/pool) / pool.vr_validation (archived: archive/tests/pool) — carries `stage-close` (the SHIP run only); their
# fresh-home fixtures carry it with them. Rows, locks, budgets and bars unchanged. vr.session_undithered (archived: archive/tests/vr)
# (7 s, the byte-identical eyes) and vr.grab_maths (archived: archive/tests/vr) stay in the merge tier; the no-runtime VR fixtures
# (pool.vr_noruntime (archived: archive/tests/pool)) are tests/scripting's.

# ---------------------------------------------------------------------------
# vr_session.verbs_session — the `vr.*` verbs through the APP, against the simulated
# runtime (VR_SPEC §6 / §4.6). The other half of vr_noruntime.vr_verbs, which proves the
# same verbs refuse cleanly with no runtime at all.
#
# vr_session.player_session — THE PLAYER IN THE HEADSET (SPECS/VR_SPEC.md §4.5, phase 3):
# the app with `--vr`, a real session, the Player page up, the mirror on the player's own
# view, the rig placed on the play camera and flown by the verb the fly keys call. The
# no-runtime half is vr_noruntime.player_vr; the rig's arithmetic is `player.vr` (archived: archive/tests/player).
#
# vr_session.photon_view — THE PHOTON VIEW WITH A HEADSET UP (PHOTON-VIEW-1 fix round,
# F6): while a stereo view draws the scene, Ogre's voxel and probe visualizers (low-level
# materials the pin's instanced stereo does not serve) are refused, naming stereo; every
# other mode stays; the refusal ends with the session.
#
# All three (and vr_session.sample_open, below) are arms of ONE pool row: one Monado
# runtime, one `--vr` process (the pool is declared at the end of this file).

# ---------------------------------------------------------------------------
# vr.mirror_live — THE DESKTOP FOLLOWS THE RUNTIME (lane MIRROR-LIVE-1; the
# owner's finding F2 of the push-#50 smoke, SPECS/VR_SPEC.md §5 phase 4).
#
# The app half of the rule `vr.session` (archived: archive/tests/vr) asserts on pixels: while the runtime
# draws eyes the editor's own View draws nothing (one render pipeline) and the
# moment it stops the desktop takes its own camera back, live — which on a real
# headset is exactly when the wearer lifts it to look at the desk. Plus the
# owner's page rule: leaving the page that hosts a session ends that session,
# in both hosts.
#
# THE BLINK HOOK IS WHAT MAKES IT ASSERTABLE. Monado never answers "no picture"
# in the middle of a live session, so the pump is told to (the frames are really
# waited for, begun and ended — only the runtime's answer is replaced):
# _BLINK_EVERY is how often, _BLINK_FRAMES how long. Twelve frames every thirty
# is a doff; ONE frame is a reprojection hiccup, and the hysteresis that tells
# them apart is `vr.session` (archived: archive/tests/vr)'s case.
set(_vr_mirror_home "${CMAKE_CURRENT_BINARY_DIR}/vr-mirror-live-home")
jah_gpu_row(vr.mirror_live SHIP CLASS vr TIER epic NEEDS photon
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_app.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${JAH_MONADO_MANIFEST}"
                 "${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_vr_mirror_live.js"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# THE FRAMES ITS LAW NEEDS (TOOLING-7 stage 2; the derivations in the script): the per-frame window is three blink cycles
# (90 frames, was 200) and the Player's hand-back two (60, was 120): 859 frames (2026-10-12, TESTING-SPEED-B4) -> 671,
# counted 2026-10-20 (B3's meter, one solo run: 46.4 s, was 62.5 s on the same fresh tree's cold cache).
set_tests_properties(vr.mirror_live PROPERTIES JAHSHAKA_TEST_FRAMES 671)
# TESTING-CLEANUP-2B item 2: TIMEOUT 330 = 6 x the run log's quiet median 53.9 s (46 quiet PASS runs since 2026-10-01, worst 168.6 s);
# declared 300 was below the policy, honest only with the +900 s widening (now deleted).
set_tests_properties(vr.mirror_live PROPERTIES
    LABELS "scripting"
    TIMEOUT 330
    RESOURCE_LOCK monado COST ${JAH_COST_MONADO}
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_vr_mirror_home};JAHSHAKA_DATA_ROOT=${_vr_mirror_home}/.local/share/Jahshaka;JAHSHAKA_VR_TEST_BLINK_EVERY=30;JAHSHAKA_VR_TEST_BLINK_FRAMES=12")

# vr.start_broken (archived: archive/tests/vr) (VR-START-1 case d): the active manifest's runtime library cannot be
# loaded. The runner still owns a private runtime (unused); the manifest is ours.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/broken-openxr-runtime.json"
     "{ \"file_format_version\": \"1.0.0\", \"runtime\": { \"name\": \"Broken\", \"library_path\": \"/nonexistent/libopenxr_broken.so\" } }\n")

# vr.frame_budget (archived: archive/tests/vr) — THE 90 Hz BAR'S RIG ARMS (lane V1-RIG item 5;
# SPECS/PHOTON_SPEC.md §11 item 8, MASTER_QUEUE §681-C).
#
# Split in two like perf.capture_bundle (archived: archive/tests/perf), and for the same reason: the app
# RECORDS (vr_frame_budget.js drives a session at the Quest Pro's eye size and
# writes two capture bundles) and a reader JUDGES what it wrote, because the
# scripting engine has no file access. The reader prints the budget lines the
# ledger wants and asserts only the rig-independent one — no frame over three
# times the run's own median. Its header says why a red against 11.1 ms on an
# Xvfb rig with free clocks would be a red about the rig.
# STAGE-CLOSE and in the GPU-timing lock (lane D6B-GATE-SHAPE; audit §3c/R3, verdict STAGE-CLOSE):
# 105 s of two capture bundles whose one assertion is a timing ratio — a measurement,
# so `quiet-box` too: no scoped gate runs it on a shared box.
set(JAH_VR_BUDGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/vr-budget-bundles")
set(JAH_VR_BUDGET_SHOWROOM "${CMAKE_SOURCE_DIR}/scenes/Showroom 2.zip")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/vr_frame_budget.js.in"
               "${CMAKE_CURRENT_BINARY_DIR}/vr_frame_budget.js" @ONLY)

# ---------------------------------------------------------------------------
# vr.eye_grade (archived: archive/tests/vr) — THE WEARER'S PICTURE IS THE PROJECT'S PICTURE (lane
# EYE-GRADE-1; SPECS/VR_SPEC.md §6).
#
# The only suite in the tree that looks at what the RUNTIME DISPLAYS. Every
# other VR suite runs Monado with no compositor at all (XRT_COMPOSITOR_NULL),
# which is right when the subject is the session's arithmetic and useless when
# the subject is colour: the two defects the #50 smoke found were the eye
# ignoring the project's grade (the session wrote its own PostFxDesc) and the
# swapchain's colour contract being inverted (a UNORM swapchain tells an OpenXR
# runtime our bytes are linear, so it encoded them a second time). Its runner
# therefore starts the compositor with an X window and captures it with xwd.
#
# THREE PIECES, like vr.frame_budget (archived: archive/tests/vr): the app RECORDS (its own eye picture, the
# desktop's screenshot, and the descriptions it asserts itself), the runner
# CAPTURES the runtime's window while the script holds a picture still, and a
# Qt reader JUDGES the files — a script cannot open a PNG.
set(JAH_EYE_GRADE_DIR "${CMAKE_CURRENT_BINARY_DIR}/vr-eye-grade-out")

get_property(_vr_rows DIRECTORY PROPERTY TESTS)
foreach(_t IN LISTS _vr_rows)
    set_property(TEST ${_t} APPEND PROPERTY LABELS vr)
endforeach()

if(TEST vr.mirror_live)
    jah_fresh_home_fixture(vr.mirror_live.fresh_home vrMirrorLiveFreshHome "${CMAKE_CURRENT_BINARY_DIR}/vr-mirror-live-home" WARM LABELS "scripting;vr")
    set_property(TEST vr.mirror_live APPEND PROPERTY FIXTURES_REQUIRED vrMirrorLiveFreshHome)
endif()
