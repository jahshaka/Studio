# Application-level self-test: the real Jahshaka binary, engine viewport, default
# scene, one screenshot. On Linux it needs a display (Ogre-Next renders through
# xcb): DISPLAY=:0 on the build machine. On macOS the viewport renders offscreen
# (no window backend yet) and forcing xcb aborts Qt at startup — no env needed.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(JAH_SELFTEST_ENV "DISPLAY=:0;QT_QPA_PLATFORM=xcb;")
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
