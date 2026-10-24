# jahlab's rows (LAB-1; SPECS/LAB_1_SPEC.md stage 1). The lab is a binary of its own (src/lab), so its rows run IT:
# each is a short driver script over the real jahlab, on the rig's display (Ogre-Next's Vulkan connects to $DISPLAY
# at boot even for an offscreen view), booting warm on the fixtures' shared shader cache (CLASS engine: the harness
# hands JAHSHAKA_SHADER_CACHE_DIR over and jahlab honours it).
if(NOT TARGET jahlab)
    return()
endif()

# lab.boots_and_shoots — `jahlab --headless --script` on the kit-bashed box set (tests/meshbake's kitBash() as an OBJ,
# kitbash_obj.py): loads, bakes into the lab's data root, Triangles view, 30 frames, a PNG, the status JSON (the cut
# drew, its pixels are on screen); a second run of the same script hits the bake cache (no parse, no bake) and cuts the
# same. Two runs of 41 frames.
jah_gpu_row(lab.boots_and_shoots SHIP CLASS engine FRAMES 82
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/boots_and_shoots.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/boots_and_shoots
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.boots_and_shoots PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.monitor (PHOTON-7, the step before stage 10) — `jahlab monitor`: the engine's own frame monitor per frame from the
# lab on photon-area (Epic, rays on): every GI phase named with a positive time, and every frame's top-level passes and
# rows adding up to its busy GPU span (monitor.sh). The boot's 30 frames, a 60-frame settle, a lamp step's 30.
jah_gpu_row(lab.monitor CLASS engine FRAMES 127
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/monitor.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/monitor
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.monitor PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.script_stops (URGENT #50) — A REFUSED SCRIPT LINE STOPS THE SCRIPT, nonzero, naming the line and its reason; a
# `try` line opts out (script_stops.sh). Three sessions of a frame on photon-contact at 320x180.
jah_gpu_row(lab.script_stops CLASS engine FRAMES 3
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/script_stops.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/script_stops
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.script_stops PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.envs_build (stage 2) — `jahlab --env-check` per environment, the eight in parallel (envs_build.sh): each builds
# (its meshes through the bake's stages) under 5 s, and rebuilds AND re-bakes deterministically: the same seed gives
# the same triangle count, scene hash and bake hash (the bake pool's threaded output), the next seed another hash. CPU.
add_test(NAME lab.envs_build COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/envs_build.sh $<TARGET_FILE:jahlab>
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(lab.envs_build)   # THE SHIP SET (TEST-CUT-1): promoted at the LAB-1 merge
set_tests_properties(lab.envs_build PROPERTIES TIMEOUT 60 LABELS "lab" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(lab.envs_build)

# lab.env_expectation_printed (stage 2) — `jahlab --env-list` prints every environment's law and its closed-form
# expectation at each dolly stop (22 / 88 / 177 / 354 m), every value a finite number. CPU only.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# lab.dolly_matches_probe (stage 3) — scripts/lab/dolly.sh on the kit-bashed box set at two stops: the dolly's table
# equals the engine's AtomDrawStatus at each stop (cut triangles, clusters, occluded objects) and the cut probe's drawn
# triangles equal that cut, +- 0. On the rig's display; the dolly takes the card's lock (re-entrant under a holder).
jah_gpu_row(lab.dolly_matches_probe SHIP CLASS engine FRAMES 30
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/dolly_matches_probe.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/dolly_matches_probe
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.dolly_matches_probe PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.stillness_runs (PHOTON-2 stage 0) — the stillness door through jahlab's `stillness` verb on photon-glass, with the
# path check, then the `move` and `lamp` verbs (stillness_runs.sh says what it asserts: the door's shape, never a bar).
# 960x540; 51 frames of move (the stop its last), 2 x 20 window frames, the 5-frame bridge, 7 frames of the verbs after.
jah_gpu_row(lab.stillness_runs CLASS engine FRAMES 104
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/stillness_runs.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/stillness_runs
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.stillness_runs PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.stillness_control (PHOTON-2 stage 0's fix round) — THE NOTHING-MOVED CONTROL: photon-room held at the stop reads
# 0 codes, no GI row and the hold from the stop on, its mask the held frame's own history count (stillness_control.sh).
# 960x540; 201 frames to the held stop, 12 after it.
jah_gpu_row(lab.stillness_control CLASS engine FRAMES 213
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/stillness_control.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/stillness_control
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.stillness_control PROPERTIES TIMEOUT 60 LABELS "lab")

# lab.camera_cut (PHOTON-2 round 2, N3) — A CAMERA CUT RESTARTS THE HISTORIES (Lumen's rule: a view axis turning > 75
# degrees or the eye moving > 100 m in a frame): on photon-glass a 150 m teleport and a 90 degree snap step the gather's
# and the reflection's cut counters by one, a 50 m teleport by none (camera_cut.sh). Three door runs of 26 frames.
jah_gpu_row(lab.camera_cut CLASS engine FRAMES 78
    COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/camera_cut.sh $<TARGET_FILE:jahlab>
            ${CMAKE_CURRENT_BINARY_DIR}/work/camera_cut
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(lab.camera_cut PROPERTIES TIMEOUT 60 LABELS "lab")
