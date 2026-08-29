# Scripting engine tests (SCRIPTING_SPEC.md phase 0+).
#
# scripting.engine — unit test of the QJSEngine host: evaluation, error line
# numbers, console capture, undo-macro wrapping, registry metadata validation.
# Links ONLY the scripting core (no MainWindow, no engine): the core is designed
# to have zero Studio dependencies (ScriptHost carries std::function probes).
# Runs offscreen.
#
# The end-to-end --script suites (full app binary, engine up) live below as
# scripting.e2e.* — added in phase 1.
find_package(Qt6 REQUIRED COMPONENTS Widgets Qml)
add_executable(test_script_engine test_script_engine.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/apimodule.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/apiregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/scriptengine.cpp)
set_target_properties(test_script_engine PROPERTIES AUTOMOC ON)
target_include_directories(test_script_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/scripting)
target_link_libraries(test_script_engine PRIVATE Qt6::Widgets Qt6::Qml)
add_test(NAME scripting.engine COMMAND test_script_engine
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(scripting.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "scripting")
