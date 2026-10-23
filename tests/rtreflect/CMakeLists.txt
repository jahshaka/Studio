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

# rtreflect.walk_world (PHOTON-7 stage 6 commit 7, the lead's ruling) — THE REFLECTION'S OWN WALKING BAR: gi.walk's glass
# walk with THE WORLD HELD (gi.freeze = 2: no cache work, the gather held, the reflections live) against the frozen walk
# - glossy and mirror M1 p99 <= 2 codes, share over one code <= 1 % (walk_world.sh); the unfrozen walk printed beside it.
# 960x540; four jahlab sessions: 7 + 3 x 710 frames.
if(TARGET jahlab)
    jah_gpu_row(rtreflect.walk_world CLASS engine TOKENS 8 FRAMES 2137
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/walk_world.sh $<TARGET_FILE:jahlab> ${CMAKE_CURRENT_BINARY_DIR}/work/walk_world
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(rtreflect.walk_world PROPERTIES TIMEOUT 60 LABELS "engine")
endif()

# rtreflect.still_after_stop (PHOTON-2 stage 1, its fix round and round 2's R1, rev 6's bars; split in round 4) — THE
# GLASS IS STILL AFTER A STOP: jahlab's stillness door on photon-glass after a walk inside one lattice cell and the
# path's teleport at equal placements — (a) the glossy change <= 0.05 codes from frame 17 and decreasing to the hold
# (J5's 0.01-code tolerance), (b) 0 codes from the hold, the hold at <= 513 rest frames, (c) the mirror's estimator
# fixed, (d) walk vs teleport on the glossy panes; (f), the 4,096-sample reference, is a measurement over a row's
# minute (still_after_stop.sh says the law and the base). 960x540; one jahlab session: 231 frames to the stop, two
# 600-frame windows and the path's bridge - 1,436 frames.
# rtreflect.lamp_step_held (rev 6's (e), split out of the row above in round 4 so each runs its law under a minute) —
# a small lamp step held at rest against a session converged at the new light, each through the door's 600-frame
# window (lamp_step_held.sh). 960x540; two jahlab sessions: the step's 300 + its door's 602 (902), the reference's
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
endif()
