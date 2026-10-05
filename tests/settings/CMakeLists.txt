# settings.store — the preferences store (src/data/settingsstore.h, SHADER-WARM-2):
# reads and writes are memory, the file is written on the store's own thread. The
# owner's phase-2 smoke froze 16 s on the Desktop page inside a QSettings sync on the
# UI thread (QLockFile behind a loaded disk); this suite holds the settings file's
# lock itself and proves the calling thread never waits for it.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_settings_store test_settings_store.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp)
set_target_properties(test_settings_store PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_settings_store PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_settings_store PRIVATE Qt6::Core)
add_test(NAME settings.store COMMAND test_settings_store
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(settings.store PROPERTIES TIMEOUT 60 LABELS "services")
jah_no_display(settings.store)
