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

# ---------------------------------------------------------------------------
# ATOM stage 2 — THE CLUSTER DAG (lane ATOM-CLUSTER-1; SPECS/atom/B2_CLUSTER_DAG_DESIGN.md).
#
# THE TWO SHIPPED HIGH-POLY MODELS, extracted at CONFIGURE time from the sample
# archives the product ships (scenes/*.zip): the Matcaps sample's Stanford dragon
# and the Physics sample's model. The fixtures read them from the build tree so
# no suite unzips anything at run time.
set(_cluster_fixture_dir ${CMAKE_CURRENT_BINARY_DIR}/cluster-fixtures)
set(_cluster_models
    "Matcaps.zip|objects/adc9ec963f064a673e5f3157b7b1a2a789025d723302c49f5fbd1df6a0b7a1ca.obj|matcaps_dragon.obj"
    "Physics.zip|objects/a9a3c340a9acfb5af1e5b090a8c89f079f8ae2be303362d30becb26b73d4d5e7.obj|physics_model.obj")
foreach(_m IN LISTS _cluster_models)
    string(REPLACE "|" ";" _parts "${_m}")
    list(GET _parts 0 _zip)
    list(GET _parts 1 _member)
    list(GET _parts 2 _name)
    if(NOT EXISTS ${_cluster_fixture_dir}/${_name})
        set(_tmp ${CMAKE_CURRENT_BINARY_DIR}/cluster-fixtures-extract)
        file(ARCHIVE_EXTRACT INPUT "${CMAKE_SOURCE_DIR}/scenes/${_zip}" DESTINATION ${_tmp}
             PATTERNS ${_member})
        file(MAKE_DIRECTORY ${_cluster_fixture_dir})
        file(RENAME ${_tmp}/${_member} ${_cluster_fixture_dir}/${_name})
        file(REMOVE_RECURSE ${_tmp})
    endif()
endforeach()

# The fixtures + (for the engine suites) the harness: a TEST TOOL, compiled into
# the executables below and into nothing of the product. The mirror is compiled
# in (as tests/assets does) because the hand-off under test IS
# SceneMirror::toMeshData.
set(_cluster_fixture_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/cluster_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
function(jah_cluster_target _t)
    set_target_properties(${_t} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_include_directories(${_t} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${_t} PRIVATE
        JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        CLUSTER_FIXTURE_DIR="${_cluster_fixture_dir}"
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${_t} PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
endfunction()
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# cluster_config_measure — the measurement the shipped clodConfig was chosen by,
# and the per-asset bake table. A TOOL (EXCLUDE_FROM_ALL), never a suite.
add_executable(cluster_config_measure EXCLUDE_FROM_ALL cluster_config_measure.cpp
    ${_cluster_fixture_sources})
jah_cluster_target(cluster_config_measure)

# atom.cluster_cut — THE RULE SELECTS ONE CUT (B2 §2): the bake's two
# monotonicities, the provenance partition, exactly-one-cut at 64 thresholds and
# from 40 (eye, tolerance) pairs, monotone refinement, and the format's round trip
# and refusal — over every shipped mesh the bake gives a DAG, the 20k sphere and
# the 40 m bar. Document-side: no engine boots, no display.
add_executable(test_atom_cluster_cut test_atom_cluster_cut.cpp ${_cluster_fixture_sources})
jah_cluster_target(test_atom_cluster_cut)
add_test(NAME atom.cluster_cut COMMAND test_atom_cluster_cut
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.cluster_cut PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "meshbake")
jah_no_display(atom.cluster_cut)

# atom.cluster_crack — THE CUT, RENDERED (B2 §3): the uploaded stream checked
# against the bake, a crack sweep (level 0 -> the root, and the view-dependent cut
# at four tolerances) on the 20k sphere, the two shipped high-poly models and
# three primitives, the dolly through the cut against the level-0 control, and
# the 40 m bar's triangle numbers against the chain. Draws through the TEST
# HARNESS (cluster_draw.*, route B2) — the product draws no cut in stage 2 — so it
# reaches EnginePrivate.h and links Ogre, as tests/shadow's spike does.
add_executable(test_atom_cluster_crack test_atom_cluster_crack.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cluster_draw.cpp ${_cluster_fixture_sources})
jah_cluster_target(test_atom_cluster_crack)
target_include_directories(test_atom_cluster_crack PRIVATE
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
target_compile_definitions(test_atom_cluster_crack PRIVATE
    CLUSTER_TEST_MEDIA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/media")
target_link_libraries(test_atom_cluster_crack PRIVATE OgreNext)
add_dependencies(test_atom_cluster_crack JahshakaEngineMedia)
add_test(NAME atom.cluster_crack COMMAND test_atom_cluster_crack
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.cluster_crack PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "engine")
