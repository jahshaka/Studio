# ATOM's ACCEPTANCE SUITES — the ones that state what the geometry pipeline must
# LOOK like rather than what it computes (SPECS/atom/A2_HONEST_GEOMETRY_AND_EVERY_
# ASSET_DESIGN.md). They boot a real engine and read real pixels, so they need a
# reachable DISPLAY (a Vulkan device on this pin cannot be created without one)
# and a Vulkan driver or lavapipe, exactly like tests/engine.

# atom.dolly_gate — AT-A7r: a camera dollies 40 m -> 2 m onto a 20k-triangle
# sphere with a five-level chain, one frame per 0.25 m, and no step's picture —
# switch or not — may differ from its predecessor by more than 3x the walk's own
# ordinary step. The bar is computed inside the run from its own non-switch
# steps, so it is a ratio and not a number that ages.
add_executable(test_atom_dolly_gate test_atom_dolly_gate.cpp)
set_target_properties(test_atom_dolly_gate PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_atom_dolly_gate PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_atom_dolly_gate PRIVATE JahshakaEngine)
add_dependencies(test_atom_dolly_gate JahshakaEngineMedia)
add_test(NAME atom.dolly_gate COMMAND test_atom_dolly_gate
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# 153 poses, each rendering one 1024x1024 frame and downloading it, plus the
# warm-up: comfortably inside 300 s on the rig (measured ~40 s) and with room
# for a contended one.
set_tests_properties(atom.dolly_gate PROPERTIES TIMEOUT 300 LABELS "engine")
