# ATOM's ACCEPTANCE SUITES — the ones that state what the geometry pipeline must
# LOOK like rather than what it computes (SPECS/atom/A2_HONEST_GEOMETRY_AND_EVERY_
# ASSET_DESIGN.md). They boot a real engine and read real pixels, so they need a
# reachable DISPLAY (a Vulkan device on this pin cannot be created without one)
# and a Vulkan driver or lavapipe, exactly like tests/engine.

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
    # ...its quick arm (one row, the destroyed material included) UNDER THE LAYER.
    if(EXISTS "/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json")
        jah_gpu_row(engine.atom_parity_validation CLASS engine COMMAND test_atom_parity
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(engine.atom_parity_validation PROPERTIES
            TIMEOUT 120 LABELS "engine"
            ENVIRONMENT "ATOM_PARITY_QUICK=1;VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;JAH_EXPECT_VALIDATION=1"
            FAIL_REGULAR_EXPRESSION "Validation Error;Validation Warning")
    endif()
    # ATOM-CPU-WALKS-1 — the per-frame CPU walks moved onto the GPU scene's change feed
    # and the device. engine.tlas_compute: the device-written top-level instance array
    # byte-equal to the CPU writer over a mixed set, its edits and the lattice.
    # atom.words_still: a still frame visits no slot and re-hashes nothing; a mover
    # frame visits its slot; an in-place material edit re-routes in one frame.
    add_executable(test_cpu_walks test_cpu_walks.cpp)
    jah_atom_engine_target(test_cpu_walks)
    jah_gpu_row(engine.tlas_compute CLASS engine COMMAND test_cpu_walks --tlas-compute
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.tlas_compute PROPERTIES TIMEOUT 300 LABELS "engine")
    jah_gpu_row(atom.words_still CLASS engine COMMAND test_cpu_walks --words-still
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(atom.words_still PROPERTIES TIMEOUT 180 LABELS "engine")
    # atom.two_sided — A TWO-SIDED MATERIAL RIDES ATOM (ATOM-TWO-SIDED-1): a closed sphere and
    # an open plane, both CULL_NONE, routed to Atom (the cut's two-sided range, the no-cull
    # id and caster pipelines, the decode's facing flip); the picture from the front and
    # from behind against stock PBS, the closed mesh's ids against its back-culled ones,
    # the open plane's shadow from both sides.
    add_executable(test_atom_two_sided test_atom_two_sided.cpp)
    jah_atom_engine_target(test_atom_two_sided)
    jah_gpu_row(atom.two_sided CLASS engine COMMAND test_atom_two_sided
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(atom.two_sided PROPERTIES TIMEOUT 180 LABELS "engine")
    # shadow.atom_cut / shadow.atom_parity — THE SHADOW MAPS' ATOM CASTERS ARE THE LIGHT'S
    # OWN CLUSTER CUT (ATOM-SHADOWS-1): every map's cut (PSSM, spot, the point's six faces)
    # captured by the engine's test door and checked against the rule at the LIGHT's request
    # and the bound (one texel of the map); and the maps against DENSE stock casters, texel
    # by texel in light space (test_shadow_atom_cut.cpp states the metrics). Pixel suites,
    # frames counted: never the GPU lock.
    add_executable(test_shadow_atom_cut test_shadow_atom_cut.cpp ${_cluster_fixture_sources})
    jah_cluster_target(test_shadow_atom_cut)
    jah_atom_engine_target(test_shadow_atom_cut)
    # EnginePrivate.h names a local `slots`: Qt's keyword macro would eat it.
    set_source_files_properties(test_shadow_atom_cut.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
    jah_gpu_row(shadow.atom_cut CLASS engine COMMAND test_shadow_atom_cut --cut
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(shadow.atom_cut PROPERTIES TIMEOUT 60 LABELS "engine")   # 7.5 s measured solo x 6, rounded up
    # shadow.caster_first_frame — CASTER-USES-1: eight views of the world render their first
    # frame together (> 64 caster maps for the scene's one stats ring): every map recorded.
    jah_gpu_row(shadow.caster_first_frame CLASS engine COMMAND test_shadow_atom_cut --first-frame
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(shadow.caster_first_frame PROPERTIES TIMEOUT 120 LABELS "engine")
    # shadow.stock_lod — A SHADOW MAP DRAWS ITS STOCK CASTERS AT THE LIGHT'S LEVEL (SHADOW-LOD-1, the
    # fork's caster-pass LOD walk): every caster pass's level for the alpha-tested stock casters is the
    # rule's at the pass's own camera and the map's texels (PSSM splits, a spot, a point's six faces), the
    # view's levels survive the shadow node's update (the save/restore), and no view pass walks the Atom
    # queue. Frames counted: never the GPU lock.
    add_executable(test_shadow_stock_lod test_shadow_stock_lod.cpp)
    jah_atom_engine_target(test_shadow_stock_lod)
    jah_gpu_row(shadow.stock_lod CLASS engine COMMAND test_shadow_stock_lod
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(shadow.stock_lod PROPERTIES TIMEOUT 60 LABELS "engine")
    add_executable(test_atom_negative_control test_atom_negative_control.cpp ${_atom_neg_headers})
    jah_atom_engine_target(test_atom_negative_control)
    target_include_directories(test_atom_negative_control PRIVATE "${_atom_neg_gen}"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
        ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(test_atom_negative_control PRIVATE "${OGRE_NEXT_VULKAN_RS_LIB}" ${Vulkan_LIBRARIES} ${CMAKE_DL_LIBS})
    # ONE ROW PER DEVICE (lane D6B-GATE-SHAPE; audit verdict MERGE): where the Khronos
    # validation layer is installed, the `_validation` rows below ARE the suite — the same
    # binary and the same assertions, plus the layer, FAIL_REGULAR_EXPRESSION on any report
    # and the positive control that the layer is active (a strict superset, the
    # app.engine_selftest precedent). The plain rows register only where the layer is
    # missing, so a box without it still runs the byte-identity assertion.
    # ...lavapipe is the second device of the spike's recipe (the software rasteriser
    # exposes neither ray queries nor the RTX's timing, so it is the other half of
    # "validation clean on both devices").
    set(_lvp_icd "/usr/share/vulkan/icd.d/lvp_icd.json")
    if(NOT EXISTS "/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json")
        add_test(NAME engine.atom_negative_control COMMAND test_atom_negative_control
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(engine.atom_negative_control PROPERTIES TIMEOUT 300 LABELS "engine")
        if(EXISTS "${_lvp_icd}")
            add_test(NAME engine.atom_negative_control_lavapipe COMMAND test_atom_negative_control
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
            set_tests_properties(engine.atom_negative_control_lavapipe PROPERTIES
                TIMEOUT 600 LABELS "engine" ENVIRONMENT "VK_ICD_FILENAMES=${_lvp_icd}")
        endif()
    endif()
    # UNDER THE VALIDATION LAYER, failing on any report: our recorder's barriers and the
    # provider's restoration are what the layer reads. Each asserts the layer is ACTIVE
    # (JAH_EXPECT_VALIDATION: Engine::validation — vkCmdDraw must resolve into the layer's library), so a
    # machine without the layer cannot read as green.
    if(EXISTS "/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json")
        jah_gpu_row(engine.atom_negative_control_validation CLASS engine COMMAND test_atom_negative_control
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(engine.atom_negative_control_validation PROPERTIES
            TIMEOUT 120 LABELS "engine"
            ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;JAH_EXPECT_VALIDATION=1"
            FAIL_REGULAR_EXPRESSION "Validation Error;Validation Warning")
        if(EXISTS "${_lvp_icd}")
            add_test(NAME engine.atom_negative_control_validation_lavapipe COMMAND test_atom_negative_control
                     WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
            set_tests_properties(engine.atom_negative_control_validation_lavapipe PROPERTIES
                TIMEOUT 120 LABELS "engine"
                ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;VK_ICD_FILENAMES=${_lvp_icd};JAH_EXPECT_VALIDATION=1"
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
jah_pool_arm(atom view ${CMAKE_CURRENT_SOURCE_DIR}/scripts/atom_view.js 12)

# ---------------------------------------------------------------------------
# atom.lod_switch — THE LOD RULES IN THE VIEWPORT (ATOM-LOD-BOUND-1; ATOM-CLUSTER-CUT). A
# scan stand-in (floor, columns, 600 debris islands; the generator atom.lod_bound_bar bakes)
# imported through the owner's route: the chain's per-object level (shadows, stock PBR)
# steps down over 30-500 m, comes back to 0 and holds across a band edge; the Levels view
# paints the CUT's depth, coarser further out; and a slow zoom shows no pop (no band). The fixture is WRITTEN by test_mesh_bake before the run (a setup test), so no
# mesh is checked in. An app run on the rig's display, like atom.view.
set(_lod_switch_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-atom_lod_switch)
set(LOD_STANDIN ${_lod_switch_home}/lod_standin.obj)
configure_file(scripts/atom_lod_switch.js.in ${CMAKE_CURRENT_BINARY_DIR}/atom_lod_switch.js @ONLY)

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "atom.two_sided=378"
        "atom.words_still=223"
        "engine.atom_negative_control_validation=123"
        "engine.atom_negative_control_validation_lavapipe=123"
        "engine.atom_parity_validation=40"
        "engine.tlas_compute=46"
        "shadow.atom_cut=80"
        "shadow.caster_first_frame=12"
        "shadow.stock_lod=71"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
