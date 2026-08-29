# Application-level self-test: the real Jahshaka binary, engine viewport, default
# scene, one screenshot. Needs a display (Ogre-Next renders through xcb), so this
# is the one suite that is not headless: DISPLAY=:0 on the build machine.
add_test(NAME app.engine_selftest
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --engine-selftest "${CMAKE_CURRENT_BINARY_DIR}/selftest.png"
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
set_tests_properties(app.engine_selftest PROPERTIES
    ENVIRONMENT "DISPLAY=:0;QT_QPA_PLATFORM=xcb"
    TIMEOUT 120
    LABELS "app")
