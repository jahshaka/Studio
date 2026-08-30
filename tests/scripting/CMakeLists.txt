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
target_include_directories(test_script_engine PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/scripting)
target_link_libraries(test_script_engine PRIVATE Qt6::Widgets Qt6::Qml)
add_test(NAME scripting.engine COMMAND test_script_engine
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(scripting.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "scripting")

# ---- end-to-end: the real app binary running --script (phase 1) ----
#
# HOME is redirected to a per-test scratch dir so the runs get a FRESH
# JahLibrary.db and projects folder and can never touch the user's live
# library. XAUTHORITY/DISPLAY are inherited from the session (the engine needs
# a reachable X display; XWayland on :0 in CI-like runs, lavapipe covers no-GPU).
function(add_script_e2e name scriptfile)
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-${name})
    file(MAKE_DIRECTORY ${_home})
    file(MAKE_DIRECTORY ${_home}/run)
    add_test(NAME scripting.e2e.${name}
             COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/${scriptfile}
             WORKING_DIRECTORY ${_home}/run)
    set_tests_properties(scripting.e2e.${name} PROPERTIES
        ENVIRONMENT "HOME=${_home}"
        LABELS "scripting"
        TIMEOUT 180)
endfunction()

add_script_e2e(build_scene e2e_build_scene.js)
add_script_e2e(undo_macro e2e_undo_macro.js)

# phase 2: full surface (assets/materials/graph/world). The script needs real
# repo paths (a mesh to import, a shipped .effect) — configured in.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(scripts/e2e_full_surface.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_full_surface.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-full_surface)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME scripting.e2e.full_surface
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_full_surface.js
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(scripting.e2e.full_surface PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "scripting"
    TIMEOUT 240)
