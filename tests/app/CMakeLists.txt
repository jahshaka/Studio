# Application-level self-test: the real Jahshaka binary, engine viewport, default
# scene, one screenshot. On Linux it needs a display (Ogre-Next renders through
# xcb): DISPLAY=:0 on the build machine. On macOS the viewport renders offscreen
# (no window backend yet) and forcing xcb aborts Qt at startup — no env needed.
# The display comes from the JAH_TEST_DISPLAY cache variable (tests/CMakeLists.txt).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(JAH_SELFTEST_ENV "DISPLAY=${JAH_TEST_DISPLAY};QT_QPA_PLATFORM=xcb;")
else()
    set(JAH_SELFTEST_ENV "")
endif()
add_test(NAME app.engine_selftest
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --engine-selftest "${CMAKE_CURRENT_BINARY_DIR}/selftest.png"
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
set_tests_properties(app.engine_selftest PROPERTIES
    ENVIRONMENT "${JAH_SELFTEST_ENV}"
    TIMEOUT 120
    LABELS "app")

# The same self-test under the Vulkan validation layer: any "Validation Error" in the
# output fails the test. This is what caught the stale depth buffer on resize.
add_test(NAME app.engine_selftest_validation
         COMMAND bash -c "\"$<TARGET_FILE:${CMAKE_PROJECT_NAME}>\" --engine-selftest \"${CMAKE_CURRENT_BINARY_DIR}/selftest_validation.png\" > \"${CMAKE_CURRENT_BINARY_DIR}/validation.log\" 2>&1; rc=$?; if grep -q 'Validation Error' \"${CMAKE_CURRENT_BINARY_DIR}/validation.log\"; then grep -m3 'Validation Error' \"${CMAKE_CURRENT_BINARY_DIR}/validation.log\"; exit 1; fi; exit $rc"
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
set_tests_properties(app.engine_selftest_validation PROPERTIES
    ENVIRONMENT "${JAH_SELFTEST_ENV}VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
    TIMEOUT 180
    LABELS "app")

# app.startup_quiet — the boot log stays clean (STABILITY_PROGRAM_SPEC lanes
# 6a/6b/6c). Three noise sources were fixed together and all three are only
# observable in a REAL boot, so this is the one suite that reads the boot log:
#
#   6b  "duplicate connection name 'qt_sql_default_connection'" — Database::
#       closeDatabase() read the connection name after invalidating the handle,
#       so removeDatabase("") no-op'd and Upgrader's schema check leaked the
#       DEFAULT connection into MainWindow::setupProjectDB.
#   6c  "invalid parameters in SMAA.material(83)" — ogre-patch 0012.
#   6a  Qt Multimedia on the startup path (iris::Scene and VideoPreviewWidget
#       both built a QMediaPlayer in their constructors). ui.media_lazy is the
#       precise guard; this one catches anything ELSE that starts constructing
#       media objects at boot. Keyed on Qt's own "qt.multimedia.ffmpeg: Using
#       Qt multimedia with FFmpeg" banner, which the backend prints when it is
#       first instantiated — a one-way guard (if a future Qt silences the
#       banner this assertion weakens, ui.media_lazy does not).
#
# TWO runs under ONE scratch HOME on purpose: the 6b warning needs a database
# to already exist, because Upgrader::checkIfSchemaNeedsUpdating() returns
# early when there is none — a single fresh-HOME boot could never see it.
# Deliberately NOT pinned to DISPLAY=:0 (unlike the selftests above): it
# inherits the session display exactly like the scripting e2e suites, so it
# runs on whatever Xvfb the gate uses.
set(_sq_home ${CMAKE_CURRENT_BINARY_DIR}/startup-quiet-home)
file(MAKE_DIRECTORY ${_sq_home}/run)
add_test(NAME app.startup_quiet
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/startup_quiet.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/startup_quiet.js"
         WORKING_DIRECTORY ${_sq_home}/run)
set_tests_properties(app.startup_quiet PROPERTIES
    ENVIRONMENT "HOME=${_sq_home}"
    TIMEOUT 300
    LABELS "app")

# app.pacing_undo — the frame-pacing loop (deep audit area 7 F8) and the Ctrl+Z
# routing on the Materials page (area 1), both against the real binary with real
# mouse and key events. The script's header says why nothing smaller can gate
# either half: the render loop is a QTimer no script can step, and shortcut
# ambiguity only exists in a live window with real widget visibility.
#
# NOTE THE MISSING DISPLAY= : this is the one suite that does NOT take
# JAH_TEST_DISPLAY. It synthesises input, and JAH_TEST_DISPLAY defaults to :0 —
# so it starts and owns a private Xvfb instead (the script explains it fully).
# Linux only, and registered only when the four tools it drives exist, so a box
# without them says so at configure time instead of failing mysteriously.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(JAH_XDOTOOL xdotool)
    find_program(JAH_CURL curl)
    find_program(JAH_JQ jq)
    find_program(JAH_XVFB Xvfb)
    if(JAH_XDOTOOL AND JAH_CURL AND JAH_JQ AND JAH_XVFB)
        set(_pu_home ${CMAKE_CURRENT_BINARY_DIR}/pacing-undo-home)
        file(MAKE_DIRECTORY ${_pu_home}/run)
        # A port of its own: mcp.e2e picks its own and both may run in one gate.
        add_test(NAME app.pacing_undo
                 COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/pacing_undo.sh"
                         "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" 8747
                 WORKING_DIRECTORY ${_pu_home}/run)
        set_tests_properties(app.pacing_undo PROPERTIES
            ENVIRONMENT "HOME=${_pu_home}"
            RUN_SERIAL TRUE
            TIMEOUT 400
            LABELS "app")
    else()
        message(STATUS "app.pacing_undo skipped: needs xdotool, curl, jq and Xvfb")
    endif()
endif()

# ---- app.scene_switch_after_play -------------------------------------------
# crash-1788594910 regression gate: switching scenes with play/simulate state
# still armed must not crash (PlayBack::stopScene + updateSceneAnimation ran
# against the closed project's torn-down scene). Real binary, scratch HOME.
set(_ssap_home ${CMAKE_CURRENT_BINARY_DIR}/scene-switch-home)
file(MAKE_DIRECTORY ${_ssap_home}/run)
add_test(NAME app.scene_switch_after_play
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scene_switch_after_play.js
         WORKING_DIRECTORY ${_ssap_home}/run)
set_tests_properties(app.scene_switch_after_play PROPERTIES
    ENVIRONMENT "HOME=${_ssap_home}"
    LABELS "app"
    TIMEOUT 300)

# ---- app.selection_outline --------------------------------------------------
# The isBuiltIn outline-suppression regression gate (2026-09-06): selecting an
# Add-menu primitive must draw the outline; selecting the ground must not.
set(_selo_home ${CMAKE_CURRENT_BINARY_DIR}/selection-outline-home)
file(MAKE_DIRECTORY ${_selo_home}/run)
add_test(NAME app.selection_outline
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_selection_outline.js
         WORKING_DIRECTORY ${_selo_home}/run)
set_tests_properties(app.selection_outline PROPERTIES
    ENVIRONMENT "HOME=${_selo_home}"
    LABELS "app"
    TIMEOUT 300)
