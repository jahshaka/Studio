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

# far_blas_measure — ATOM-FARBLAS-1's numbers: the far BLAS's memory per shipped
# mesh and the lattice's TLAS size / build / refit. A TOOL (EXCLUDE_FROM_ALL).
add_executable(far_blas_measure EXCLUDE_FROM_ALL far_blas_measure.cpp
    ${_cluster_fixture_sources})
jah_cluster_target(far_blas_measure)
add_dependencies(far_blas_measure JahshakaEngineMedia)

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

# ---------------------------------------------------------------------------
# ATOM stage 3 — THE PARITY GRID and THE NEGATIVE CONTROL (lane ATOM-S3-PARITY;
# SPECS/atom/D1_HLMS_ATOM_AND_VISBUF_DESIGN.md §1). Both reach past the public
# Engine API (EnginePrivate.h, HlmsAtom.h, AtomPass.h — as atom.cluster_crack
# does) and link Ogre: the grid builds its own workspaces on the scene's manager,
# the control records raw Vulkan through the engine's pass provider.
function(jah_atom_engine_target _t)
    set_target_properties(${_t} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_include_directories(${_t} PRIVATE
        "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
    target_compile_definitions(${_t} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${_t} PRIVATE JahshakaEngine OgreNext)
    add_dependencies(${_t} JahshakaEngineMedia)
endfunction()

# engine.atom_parity — the grid: materials x lights x Photon terms, stock HlmsPbs
# against HlmsAtom's decode over a hand-made id buffer, per cell mean <= 1.0 code
# and <= 0.5 % of interior pixels beyond 8 codes, a grazing cell included. It reads
# until every arm is stable (frames, never time) and prints the whole grid.

# engine.atom_negative_control — stage 0's P-B, permanent: our own compute + graphics
# pipeline recorded into Ogre's frame from the engine's PASS_CUSTOM provider, and
# Ogre's next pass byte-identical to the same pass without ours over 120 frames. Its
# three shaders compile to SPIR-V at BUILD time (never committed bytes), and the
# suite links the Vulkan render system to reach the device, like tests/vr's spike.
find_package(Vulkan QUIET)
find_program(JAH_ATOM_GLSLANG NAMES glslangValidator glslang)
if(Vulkan_FOUND AND JAH_ATOM_GLSLANG AND OGRE_NEXT_VULKAN_RS_LIB)
    set(_atom_neg_gen "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${_atom_neg_gen}")
    set(_atom_neg_headers)
    foreach(_stage comp vert frag)
        add_custom_command(
            OUTPUT "${_atom_neg_gen}/atom_negative_${_stage}_spv.h"
            COMMAND "${JAH_ATOM_GLSLANG}" -V -S ${_stage} --vn test_${_stage}
                    -o "${_atom_neg_gen}/atom_negative_${_stage}_spv.h"
                    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atom_negative.${_stage}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/atom_negative.${_stage}"
            COMMENT "Compiling engine.atom_negative_control's ${_stage} shader"
            VERBATIM)
        list(APPEND _atom_neg_headers "${_atom_neg_gen}/atom_negative_${_stage}_spv.h")
    endforeach()
    add_executable(test_atom_parity test_atom_parity.cpp)
    jah_atom_engine_target(test_atom_parity)
    target_include_directories(test_atom_parity PRIVATE
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
        ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(test_atom_parity PRIVATE "${OGRE_NEXT_VULKAN_RS_LIB}" ${Vulkan_LIBRARIES} ${CMAKE_DL_LIBS})
    add_test(NAME engine.atom_parity COMMAND test_atom_parity
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # 220 cells in 20 arms, each read until stable, plus a material destroyed mid-grid.
    set_tests_properties(engine.atom_parity PROPERTIES TIMEOUT 300 LABELS "engine")
    # ...its quick arm (one row, the destroyed material included) UNDER THE LAYER.
    if(EXISTS "/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json")
        add_test(NAME engine.atom_parity_validation COMMAND test_atom_parity
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(engine.atom_parity_validation PROPERTIES
            TIMEOUT 600 LABELS "engine"
            ENVIRONMENT "ATOM_PARITY_QUICK=1;VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;JAH_ATOM_EXPECT_VALIDATION=1"
            FAIL_REGULAR_EXPRESSION "Validation Error;Validation Warning")
    endif()
    # engine.atom_draw — THE VISIBILITY BUFFER AS THE PRODUCT'S OPAQUE PATH (ATOM
    # S3-DRAW): an ordinary view of the parity grid whose chain carries the id pass
    # and the screen decode, against the same view through stock PBS (the split's
    # measurement door shut); the ids against the CPU raster; the split's stat; the
    # buckets. ATOM_DRAW_QUICK=1 runs the direct arm alone (a diagnosis switch).
    add_executable(test_atom_draw test_atom_draw.cpp)
    jah_atom_engine_target(test_atom_draw)
    target_compile_definitions(test_atom_draw PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME engine.atom_draw COMMAND test_atom_draw
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.atom_draw PROPERTIES TIMEOUT 600 LABELS "engine")
    add_executable(test_atom_negative_control test_atom_negative_control.cpp ${_atom_neg_headers})
    jah_atom_engine_target(test_atom_negative_control)
    target_include_directories(test_atom_negative_control PRIVATE "${_atom_neg_gen}"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
        ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(test_atom_negative_control PRIVATE "${OGRE_NEXT_VULKAN_RS_LIB}" ${Vulkan_LIBRARIES} ${CMAKE_DL_LIBS})
    add_test(NAME engine.atom_negative_control COMMAND test_atom_negative_control
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.atom_negative_control PROPERTIES TIMEOUT 300 LABELS "engine")
    # ...on lavapipe, the second device of the spike's recipe (the software rasteriser
    # exposes neither ray queries nor the RTX's timing, so it is the other half of
    # "validation clean on both devices").
    set(_lvp_icd "/usr/share/vulkan/icd.d/lvp_icd.json")
    if(EXISTS "${_lvp_icd}")
        add_test(NAME engine.atom_negative_control_lavapipe COMMAND test_atom_negative_control
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(engine.atom_negative_control_lavapipe PROPERTIES
            TIMEOUT 600 LABELS "engine" ENVIRONMENT "VK_ICD_FILENAMES=${_lvp_icd}")
    endif()
    # ...and both UNDER THE VALIDATION LAYER, failing on any report: our recorder's
    # barriers and the provider's restoration are what the layer reads. Each asserts
    # the layer is ACTIVE (JAH_ATOM_EXPECT_VALIDATION: vkCmdDraw must resolve into
    # the layer's library), so a machine without the layer cannot read as green.
    if(EXISTS "/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json")
        add_test(NAME engine.atom_negative_control_validation COMMAND test_atom_negative_control
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(engine.atom_negative_control_validation PROPERTIES
            TIMEOUT 600 LABELS "engine"
            ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;JAH_ATOM_EXPECT_VALIDATION=1"
            FAIL_REGULAR_EXPRESSION "Validation Error;Validation Warning")
        if(EXISTS "${_lvp_icd}")
            add_test(NAME engine.atom_negative_control_validation_lavapipe COMMAND test_atom_negative_control
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
            set_tests_properties(engine.atom_negative_control_validation_lavapipe PROPERTIES
                TIMEOUT 900 LABELS "engine"
                ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;VK_ICD_FILENAMES=${_lvp_icd};JAH_ATOM_EXPECT_VALIDATION=1"
                FAIL_REGULAR_EXPRESSION "Validation Error;Validation Warning")
        endif()
    endif()
else()
    message(STATUS "Jahshaka: engine.atom_negative_control NOT built — vulkan=${Vulkan_FOUND} "
                   "glslang='${JAH_ATOM_GLSLANG}' vulkanRs='${OGRE_NEXT_VULKAN_RS_LIB}'")
endif()

# ---------------------------------------------------------------------------
# atom.view — THE ATOM VIEW (lane D0-ATOM-VIEW): world.setAtomView's four false-colour
# modes over the default scene in both chain shapes (the "scene" grade's post chain,
# the "plain" grade's passthrough), 'off' pixel-identical after them, nothing painted
# with the split shut, and a stock-PBR object in front of the floor left lit (the
# depth-equality test). Every shot is 160 x 90 with every pixel probed. An app run on
# the rig's display, like the scripting e2e suites.
set(_atom_view_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-atom_view)
file(MAKE_DIRECTORY ${_atom_view_home}/run)
add_test(NAME atom.view
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/atom_view.js
         WORKING_DIRECTORY ${_atom_view_home}/run)
# 22 s solo on the rig (twenty-three settled shots); the rest is gate-load headroom.
set_tests_properties(atom.view PROPERTIES
    TIMEOUT 300 LABELS "scripting"
    ENVIRONMENT "HOME=${_atom_view_home};JAHSHAKA_DATA_ROOT=${_atom_view_home}/.local/share/Jahshaka")
