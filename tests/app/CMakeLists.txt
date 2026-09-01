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
