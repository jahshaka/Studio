# THE Qt-FREE CORE's HELPERS (NOQT-CORE-1 stage 1; irisgl/core): one CPU row per helper, each its own small binary linking
# IrisCore alone (core.bound also QtCore: qBound is its reference, like math.parity). Each file's header says its laws.
foreach(_t text files guid signal)
    add_executable(test_core_${_t} test_core_${_t}.cpp)
    set_target_properties(test_core_${_t} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(test_core_${_t} PRIVATE IrisCore)
    target_include_directories(test_core_${_t} PRIVATE ${CMAKE_SOURCE_DIR})   # "irisgl/core/..." (source.engine_headers H5)
endforeach()
find_package(Threads REQUIRED)
target_link_libraries(test_core_files PRIVATE Threads::Threads)
target_link_libraries(test_core_guid PRIVATE Threads::Threads)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_core_bound test_core_bound.cpp)
set_target_properties(test_core_bound PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(test_core_bound PRIVATE IrisCore Qt6::Core)
target_include_directories(test_core_bound PRIVATE ${CMAKE_SOURCE_DIR})

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
foreach(_t text files signal bound)
    add_test(NAME core.${_t} COMMAND test_core_${_t} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
endforeach()
# core.guid: the form over 100,000 drawn, and every stored key of the seeded template library (tests/.seedhome, the warm-up's):
# the text form for all, newGuid's form for every key that is not a shipped fixed id (read from src/data/primitives.h)
add_test(NAME core.guid
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/guid_library.py $<TARGET_FILE:test_core_guid>
                 ${CMAKE_BINARY_DIR}/tests/.seedhome/data/JahLibrary.db ${CMAKE_SOURCE_DIR}/src/data/primitives.h
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
# its binary is on its command line; the seeded library it reads is the warm-up's (tests/support/seed_home.sh), no build target
jah_builds(none ROWS core.guid)
set_tests_properties(core.text core.files core.guid core.signal core.bound PROPERTIES TIMEOUT 60 LABELS "core")
jah_no_display(core.text core.files core.guid core.signal core.bound)
