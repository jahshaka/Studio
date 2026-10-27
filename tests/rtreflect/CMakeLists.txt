# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
# Ray-traced reflections (SPECS/PHOTON_SPEC.md §7 R5) — headless, framework-free,
# links JahshakaEngine only. Same runtime requirements as tests/ssr and
# tests/gi: a reachable DISPLAY and a Vulkan driver.
#
# TWO ENTRIES, ONE BINARY, and that is the point: `gi.rt_reflect` (archived: archive/tests/gi) is the traced
# picture and `gi.rt_reflect_norays` (archived: archive/tests/gi) is the picture a machine WITHOUT ray
# queries draws, proved on the same GPU through the tier's own switch. A machine
# that has no ray queries at all skips the first cleanly.
function(jahshaka_rt_reflect_test target)
    add_executable(${target} test_rt_reflect.cpp)
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
endfunction()

if(JAHSHAKA_ASAN)
    # THE SANITISED TWIN for the new pass. `protect_shadow_gap=0` is already in
    # JAH_ASAN_ENGINE_OPTIONS and is load-bearing here: the NVIDIA driver
    # refuses vkCreateDevice with the ray-tracing feature bits enabled under
    # ASan's default shadow gap (PHOTON R1, deviation (a)) — with the gap
    # unprotected the device comes up and the 1,900-line raw-Vulkan tier gets
    # the coverage R1 had to do without.
    jahshaka_rt_reflect_test(test_rt_reflect_asan)
    target_link_libraries(test_rt_reflect_asan PRIVATE JahshakaEngineAsan)
    target_compile_options(test_rt_reflect_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_rt_reflect_asan PRIVATE -fsanitize=address)
    jah_gpu_row(gi.rt_reflect_asan SHIP CLASS engine COMMAND test_rt_reflect_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(gi.rt_reflect_asan PROPERTIES
                         TIMEOUT 60 LABELS "engine"
                         ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(gi.rt_reflect_asan PROPERTIES JAHSHAKA_TEST_FRAMES 939)
endif()


# rtreflect.history_detectors — THE REFLECTION HISTORY'S TWO DETECTORS (PHOTON-5 stage 4): a lamp step reaches a
# still glossy floor's reflection within 2 x kWarmupFrames frames (the radiance cap); a 0.25 m slide leaves no trail (a guard).
add_executable(test_rtreflect_history_detectors test_rtreflect_history_detectors.cpp)
set_target_properties(test_rtreflect_history_detectors PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_rtreflect_history_detectors PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_rtreflect_history_detectors PRIVATE JahshakaEngine)
add_dependencies(test_rtreflect_history_detectors JahshakaEngineMedia)
jah_gpu_row(rtreflect.history_detectors CLASS engine COMMAND test_rtreflect_history_detectors
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.history_detectors PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(rtreflect.history_detectors PROPERTIES JAHSHAKA_TEST_FRAMES 578)
jah_ship_row(rtreflect.history_detectors)   # THE SHIP SET: PHOTON-5's end-to-end row (the lead's promotion at the merge)

# rtreflect.emitters — THE EMITTER TABLE (PHOTON-7 stage 11, 11-1a): the reflection's NEE targets as the hit table carries
# them - seven emissive pillars are seven targets (their row's radiance bit for bit, their world box's sphere, ordered by
# slot); a move is no new set; the pillars dark are zero (the negative control); the tier's cap keeps the brightest and
# counts the rest.
add_executable(test_rtreflect_emitters test_rtreflect_emitters.cpp)
set_target_properties(test_rtreflect_emitters PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_rtreflect_emitters PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_rtreflect_emitters PRIVATE JahshakaEngine)
add_dependencies(test_rtreflect_emitters JahshakaEngineMedia)
jah_gpu_row(rtreflect.emitters CLASS engine COMMAND test_rtreflect_emitters
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.emitters PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(rtreflect.emitters PROPERTIES JAHSHAKA_TEST_FRAMES 57)

# rtreflect.sphere_glint — A LAMP HAS A SIZE (PHOTON-7 stage 11, the sphere-light glint): a polished metal floor under one
# point lamp, the pixel's direct specular alone; the glint's half-maximum disc at R 0.1 m within 0.8-2.0 x the lamp's
# angular radius through the mirror, R 0.1 against R 0.05 1.5-2.5 x, a point (R 0.001) under half R 0.05's.
add_executable(test_sphere_glint test_sphere_glint.cpp)
set_target_properties(test_sphere_glint PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_sphere_glint PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_sphere_glint PRIVATE JahshakaEngine)
add_dependencies(test_sphere_glint JahshakaEngineMedia)
jah_gpu_row(rtreflect.sphere_glint CLASS engine COMMAND test_sphere_glint
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.sphere_glint PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")
set_tests_properties(rtreflect.sphere_glint PROPERTIES JAHSHAKA_TEST_FRAMES 37)
# rtreflect.sphere_glint_energy (CPU) — A SPHERE LIGHT'S GLINT CARRIES THE POINT'S ENERGY: the fork's JahBrdf read (its k
# and its table of mu T), its jahSphereLightWiden mirrored, the widened glint's energy integrated over view directions
# against the exact sphere (4,096 cone directions) - within 1 % at cos 0.6 / 0.9, R/d 0.01 / 0.04 / 0.1, roughness 0.05
# and 0.2; Karis's representative point (the form built first) out of 1 % at cos 0.6 (the negative control).
add_executable(test_sphere_glint_energy test_sphere_glint_energy.cpp)
set_target_properties(test_sphere_glint_energy PROPERTIES CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_sphere_glint_energy PRIVATE
    JAH_FORK_HLMS_PBS_ANY="${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next/Samples/Media/Hlms/Pbs/Any")
add_test(NAME rtreflect.sphere_glint_energy COMMAND test_sphere_glint_energy WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(rtreflect.sphere_glint_energy PROPERTIES LABELS "engine" TIMEOUT 60)
jah_no_display(rtreflect.sphere_glint_energy)

# rtreflect.far_record (PHOTON-7 stage 6, READ_B finding 1) — A FAR HIT IS RECORDED AS A FAR HIT: a mirror's reflection of a
# two-level sphere 93 m behind the camera (past the 50 m near length: the far copies, a record) against the sphere seen
# directly from the mirrored camera - its lit and dark halves where they are, not the shuffled level-0 triangles a
# near-flagged record decodes. 256x144, 66 frames.
add_executable(test_far_record test_far_record.cpp)
set_target_properties(test_far_record PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_far_record PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_far_record PRIVATE JahshakaEngine)
add_dependencies(test_far_record JahshakaEngineMedia)
jah_gpu_row(rtreflect.far_record CLASS engine COMMAND test_far_record
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.far_record PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(rtreflect.far_record PROPERTIES JAHSHAKA_TEST_FRAMES 66)

# rtreflect.tilted_normal (PHOTON-7 stage 6, READ_B finding 6) — A SHADING NORMAL FAR FROM THE SURFACE NEVER VOIDS THE
# FOLD: a mirror floor whose vertex normals tilt 70 degrees still reflects the wall its drawn plane faces (the geometric
# normal falls back to the shading one only where the depth's reconstruction fails), against the untilted control.
# 256x144, 61 frames.
add_executable(test_tilted_normal test_tilted_normal.cpp)
set_target_properties(test_tilted_normal PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_tilted_normal PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_tilted_normal PRIVATE JahshakaEngine)
add_dependencies(test_tilted_normal JahshakaEngineMedia)
jah_gpu_row(rtreflect.tilted_normal CLASS engine COMMAND test_tilted_normal
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.tilted_normal PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(rtreflect.tilted_normal PROPERTIES JAHSHAKA_TEST_FRAMES 61)

# rtreflect.hit_area_shadow (PHOTON-7, 7-H at the photon-7r merge; L9) — AN AREA LIGHT SEEN IN A REFLECTION IS SHADOWED BY
# THE STRATA LAW: a matte floor under a screen with a 0.1 m hole on an area light's centre line, seen in a mirror, reads
# the unscreened floor times the analytic form-factor ratio through the hole (R12; declared red: the reflection's
# history under-reads the sparse sample - test_hit_area_shadow.cpp) - RED before 7-H, whose one ray to the centre
# passed the hole. Two arms (the screen hidden, shown) in one process; 256x144.
add_executable(test_hit_area_shadow test_hit_area_shadow.cpp)
set_target_properties(test_hit_area_shadow PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_hit_area_shadow PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_hit_area_shadow PRIVATE JahshakaEngine)
add_dependencies(test_hit_area_shadow JahshakaEngineMedia)
jah_gpu_row(rtreflect.hit_area_shadow CLASS engine COMMAND test_hit_area_shadow
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.hit_area_shadow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(rtreflect.hit_area_shadow PROPERTIES JAHSHAKA_TEST_FRAMES 160)

# rtreflect.odd_size (PHOTON-7 stage 6 commit 7) — EVERY PIXEL OF THE REFLECTION IS WRITTEN EVERY FRAME: an odd 257 x 145
# view at half-resolution rays, its remainder column and row against their neighbours over a glossy floor's reflection.
# 91 frames.
add_executable(test_odd_size test_odd_size.cpp)
set_target_properties(test_odd_size PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_odd_size PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_odd_size PRIVATE JahshakaEngine)
add_dependencies(test_odd_size JahshakaEngineMedia)
jah_gpu_row(rtreflect.odd_size CLASS engine COMMAND test_odd_size
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(rtreflect.odd_size PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine" SKIP_RETURN_CODE 77)
set_tests_properties(rtreflect.odd_size PROPERTIES JAHSHAKA_TEST_FRAMES 91)

# rtreflect.walk_world / rtreflect.walk_world_both (PHOTON-7 stage 6 commit 7; the lead's rulings: two labelled arms after
# REFLECT-MOVING-1, round 5's measures) — THE REFLECTION'S WALKING BARS: gi.walk's glass walk with THE WORLD HELD
# (gi.freeze = 2: no cache work, the gather held, the reflections live), seeds 0 and 7919, against the frozen walk -
# GLOSSY by the NOISE MAP (rA - rB) / sqrt 2 and THE MIRROR by whole M1, each p99 <= 2 codes and share over one code
# <= 1 % (whole glossy M1 printed as information: the noise plus the signal) (walk_world.sh, walk_noise.py). One arm a
# row, its mode asserted: walk_world = Rays with the hits flattened, the reflection's own bar (stage 6's); walk_world_both
# = Both on real hits, REFLECT-MOVING-1's Both half (the defect's slide and lag are owed before PHOTON-7's master check on
# the merged tree). 960x540; four jahlab sessions each: 6 + 3 x 570 frames.
if(TARGET jahlab)
    jah_gpu_row(rtreflect.walk_world CLASS engine TOKENS 8 FRAMES 1716
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/walk_world.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/walk_world
                raysflat
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.walk_world PROPERTIES TIMEOUT 60 LABELS "engine")
    jah_gpu_row(rtreflect.walk_world_both CLASS engine TOKENS 8 FRAMES 1716
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/walk_world.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/walk_world_both both
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.walk_world_both PROPERTIES TIMEOUT 60 LABELS "engine")
    # rtreflect.lag_law (PHOTON-7 stage 6, REFLECT-MOVING-1's row) — THE MOVING REFLECTION LAGS ITS CONVERGED PICTURE BY
    # NO MORE THAN ITS MEMORY AND ITS NOISE: the glass walk in Rays with the hits flattened and the world held, per glossy
    # pixel |walk - conv| <= (N - 1) |conv(k+1) - conv(k)| + 3 SE_walk + 1 at walk frames 14 / 28 / 59, the breaking
    # share <= 1 % (lag_law.sh; SE_walk through the door: the arm reflect.historyReadback's moving SE). 960x540; three
    # jahlab sessions: 6 + 709 + 4,240 frames.
    jah_gpu_row(rtreflect.lag_law CLASS engine TOKENS 8 FRAMES 4955
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/lag_law.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/lag_law
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.lag_law PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# rtreflect.still_after_stop (PHOTON-2 stage 1, its fix round and round 2's R1, rev 6's bars; split in round 4) — THE
# GLASS IS STILL AFTER A STOP: jahlab's stillness door on photon-glass after a walk inside one lattice cell and the
# path's teleport at equal placements — (a) the glossy change <= 0.05 codes from frame 17 and decreasing to the hold
# (J5's 0.01-code tolerance), (b') the hold inside the window, 0 codes from it, no capped texel at it, (c) the mirror's estimator
# fixed, (d) walk vs teleport on the glossy panes; (f), the 4,096-sample reference, is a measurement over a row's
# minute (still_after_stop.sh says the law and the base). 960x540; one jahlab session: 231 frames to the stop, two
# 600-frame windows and the path's bridge - 1,436 frames.
# rtreflect.lamp_step_held (rev 6's (e), split out of the row above in round 4 so each runs its law under a minute) —
# a small lamp step held at rest against a session converged at the new light, each through the door's 600-frame
# window, each hold with no capped texel (lamp_step_held.sh). 960x540; two jahlab sessions: the step's 300 + its door's 602 (902), the reference's
# 621 - 1,523 frames.
if(TARGET jahlab)
    jah_gpu_row(rtreflect.still_after_stop CLASS engine FRAMES 1436
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/still_after_stop.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/still_after_stop
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.still_after_stop PROPERTIES TIMEOUT 60 LABELS "engine")
    jah_gpu_row(rtreflect.lamp_step_held CLASS engine FRAMES 1523
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/lamp_step_held.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/lamp_step_held
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.lamp_step_held PROPERTIES TIMEOUT 60 LABELS "engine")
    # rtreflect.nee_firefly (PHOTON-7 stage 11, 11-1b) - THE REFLECTION FINDS ITS EMITTERS BY NEXT-EVENT ESTIMATION:
    # photon-firefly (one small bright emitter over a rough metal plate), Both, the arm reflect.nee on and off: (a) the
    # table's one emitter, NEE rays with it on and none off, the cone's hit rate printed; (b) at rest frame 300 NEE's
    # still-converging texels at most a quarter of the BRDF-only's; (c) NEE's hold inside the window with no capped
    # texel, the control's none (nee_firefly.sh). 480x270; two jahlab sessions - about 1,500 frames.
    jah_gpu_row(rtreflect.nee_firefly CLASS engine FRAMES 1500
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/nee_firefly.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/nee_firefly
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.nee_firefly PROPERTIES TIMEOUT 60 LABELS "engine")
    # rtreflect.contact (PHOTON-7 stage 6, AB E15 / ONE_PICTURE C3; READ_A findings 1-2) - THE CONTACT MEETS ITS
    # REFLECTION AND NO RAY HITS ITS OWN SURFACE: the post foot's reflection starts at the contact line and its top edge
    # lies where the mirror puts it (Rays: the lifted mirror; Both: the interval to the plain one), within 1 px, from
    # 2.4 m and 1 m on photon-ramp; on photon-contact's CHAINED, CORRUGATED floor (parts of Atom's cut lie below level 0)
    # and the FLAT level-0 mirror beside it the seam, no dark pixel where the floors reflect the panel, no dark line
    # along a crease; at EACH distance rays.originCut 0 green on both level-0 floors (the float-only control) and
    # rays.originPoint 0 red on the chained floor at 2.4 m (contact.sh). 1920x1080; two jahlab sessions, eleven
    # shots of 50 frames - 550 frames.
    jah_gpu_row(rtreflect.contact CLASS engine FRAMES 550
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/contact.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/contact
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.contact PROPERTIES TIMEOUT 60 LABELS "engine")
    # rtreflect.mirror_blend (PHOTON-7 stage 6, READ_B F2) - THE MIRROR TAKES THE COIN'S EXPECTATION: on photon-glass's
    # mirror, seen from 3 m up at three poses of a slow pan (where the camera-reversal fade crosses one half inside it),
    # the screen's share of the Both picture (Both - Rays) never steps from nothing to the whole screen-ray difference
    # between neighbouring pixels away from the ray picture's edges; the base's fixed coin did, along a seam
    # (mirror_blend.sh). 960x540; one jahlab session, 108 frames.
    jah_gpu_row(rtreflect.mirror_blend CLASS engine FRAMES 108
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/mirror_blend.sh $<TARGET_FILE:jahlab>
                ${CMAKE_CURRENT_BINARY_DIR}/work/mirror_blend
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.mirror_blend PROPERTIES TIMEOUT 60 LABELS "engine")
endif()
