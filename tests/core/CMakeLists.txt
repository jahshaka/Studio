# THE Qt-FREE CORE's HELPERS (NOQT-CORE-1 stage 1; irisgl/core): one CPU row per helper, each its own small binary linking
# IrisCore alone (core.bound also QtCore: qBound is its reference, like math.parity). Each file's header says its laws.
foreach(_t text files guid signal json log)
    add_executable(test_core_${_t} test_core_${_t}.cpp)
    set_target_properties(test_core_${_t} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(test_core_${_t} PRIVATE IrisCore)
    target_include_directories(test_core_${_t} PRIVATE ${CMAKE_SOURCE_DIR})   # "irisgl/core/..." (source.engine_headers H5)
endforeach()
find_package(Threads REQUIRED)
target_link_libraries(test_core_files PRIVATE Threads::Threads)
target_link_libraries(test_core_guid PRIVATE Threads::Threads)
target_link_libraries(test_core_log PRIVATE Threads::Threads)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_core_bound test_core_bound.cpp)
set_target_properties(test_core_bound PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(test_core_bound PRIVATE IrisCore Qt6::Core)
target_include_directories(test_core_bound PRIVATE ${CMAKE_SOURCE_DIR})

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
# core.json: the ONE JSON codec's laws (irisgl/core/json.h; moved from the script host as script.json, NOQT-CORE-1 stage 3)
# core.log: the ONE log's laws (irisgl/core/log.h; NOQT-CORE-1 stage 2): records serialised, the stderr default, no file
foreach(_t text files signal bound json log)
    add_test(NAME core.${_t} COMMAND test_core_${_t} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
endforeach()
# core.guid: the form over 100,000 drawn, and every stored key of the seeded template library (tests/.seedhome, the warm-up's):
# the text form for all, newGuid's form for every key that is not a shipped fixed id (read from src/data/primitives.h)
add_test(NAME core.guid
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/guid_library.py $<TARGET_FILE:test_core_guid>
                 ${CMAKE_BINARY_DIR}/tests/.seedhome/data/JahLibrary.db ${CMAKE_SOURCE_DIR}/src/data/primitives.h
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
# core.json_qt_parity: the one codec writes Qt's text (option B; test_core_json_qt_parity.cpp says the laws) - the number edges,
# NaN / infinity refused by name, the empty containers, and every JSON blob of the seeded template library (tests/.seedhome).
# Its process LINKS QtCore on purpose: QJsonDocument is the reference, beside iris::json. It lives until the saved scene's
# format is rewritten (plan 0.16).
add_executable(test_core_json_qt_parity test_core_json_qt_parity.cpp)
set_target_properties(test_core_json_qt_parity PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(test_core_json_qt_parity PRIVATE IrisCore Qt6::Core)
target_include_directories(test_core_json_qt_parity PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME core.json_qt_parity
         COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/json_qt_parity.py $<TARGET_FILE:test_core_json_qt_parity>
                 ${CMAKE_BINARY_DIR}/tests/.seedhome/data/JahLibrary.db
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
# its binary is on its command line; the seeded library it reads is the warm-up's (tests/support/seed_home.sh), no build target
jah_builds(none ROWS core.guid core.json_qt_parity)
set_tests_properties(core.text core.files core.guid core.signal core.bound core.json core.json_qt_parity core.log PROPERTIES
                     TIMEOUT 60 LABELS "core")
jah_no_display(core.text core.files core.guid core.signal core.bound core.json core.json_qt_parity core.log)
