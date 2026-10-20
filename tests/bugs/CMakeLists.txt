# bugs.gi_open_frame (BUGS-6 stage 0; SPECS/BUGS_6_SPEC.md) — a scene create lands no lighting work on a
# presented frame: the arm built under the open, no compute compile after the reveal, no scroll target
# cloned across the create. The law and the derivation of its 60 frames: gi_open_frame.js's header.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_gi_open_frame test_gi_open_frame.cpp)
set_target_properties(test_gi_open_frame PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_gi_open_frame PRIVATE Qt6::Core)
target_compile_definitions(test_gi_open_frame PRIVATE
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
set(_gi_open_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-gi-open-frame)
file(MAKE_DIRECTORY ${_gi_open_home}/run)
jah_gpu_row(bugs.gi_open_frame CLASS app TIER epic NEEDS photon FRAMES 60
         COMMAND test_gi_open_frame ${CMAKE_CURRENT_SOURCE_DIR}/gi_open_frame.js WORKING_DIRECTORY ${_gi_open_home}/run)
set_tests_properties(bugs.gi_open_frame PROPERTIES
    ENVIRONMENT "HOME=${_gi_open_home};JAHSHAKA_DATA_ROOT=${_gi_open_home}/.local/share/Jahshaka"
    TIMEOUT 60 LABELS "bugs")
jah_tsan_blocked(bugs.gi_open_frame)   # spawns the app: see jah_tsan_blocked
if(TEST bugs.gi_open_frame)
    jah_fresh_home_fixture(bugs.gi_open_frame.fresh_home giOpenFrameFreshHome "${_gi_open_home}" WARM LABELS "bugs")
    set_property(TEST bugs.gi_open_frame APPEND PROPERTY FIXTURES_REQUIRED giOpenFrameFreshHome)
endif()
