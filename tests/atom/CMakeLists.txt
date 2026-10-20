
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

find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ---------------------------------------------------------------------------
# ATOM stage 3 — THE PARITY GRID and THE NEGATIVE CONTROL (lane ATOM-S3-PARITY;
# SPECS/atom/D1_HLMS_ATOM_AND_VISBUF_DESIGN.md §1). Both reach past the public
# Engine API (EnginePrivate.h, HlmsAtom.h, AtomPass.h — as atom.cluster_crack (archived: archive/tests/atom)
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

# engine.atom_parity (archived: archive/tests/engine) — the grid: materials x lights x Photon terms, stock HlmsPbs
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
    # ...AND ITS SANITISED TWIN: the owners' retire windows (a destroyed view's stats ring and a
    # destroyed scene's caster ring, reaped 8 frames later through the device's window) are raw
    # Vulkan memory handed between owners — the use-after-free class a sanitizer is for.
    if(JAHSHAKA_ASAN)
        add_executable(test_atom_component_asan test_atom_component.cpp)
        set_target_properties(test_atom_component_asan PROPERTIES
            AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
            INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
        target_compile_definitions(test_atom_component_asan PRIVATE
            JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
            JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
        target_link_libraries(test_atom_component_asan PRIVATE JahshakaEngineAsan)
        add_dependencies(test_atom_component_asan JahshakaEngineMedia)
        target_compile_options(test_atom_component_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(test_atom_component_asan PRIVATE -fsanitize=address)
        jah_gpu_row(atom.component_asan SHIP CLASS engine COMMAND test_atom_component_asan
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(atom.component_asan PROPERTIES TIMEOUT 120 LABELS "engine"
            ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
        set_tests_properties(atom.component_asan PROPERTIES JAHSHAKA_TEST_FRAMES 37)   # its plain twin's
    endif()
    # EnginePrivate.h names a local `slots`: Qt's keyword macro would eat it.
    set_source_files_properties(test_shadow_atom_cut.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
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
else()
    message(STATUS "Jahshaka: engine.atom_negative_control NOT built — vulkan=${Vulkan_FOUND} "
                   "glslang='${JAH_ATOM_GLSLANG}' vulkanRs='${OGRE_NEXT_VULKAN_RS_LIB}'")
endif()

# ---------------------------------------------------------------------------
# atom.lod_switch (archived: archive/tests/atom) — THE LOD RULES IN THE VIEWPORT (ATOM-LOD-BOUND-1; ATOM-CLUSTER-CUT). A
# scan stand-in (floor, columns, 600 debris islands; the generator atom.lod_bound_bar (archived: archive/tests/atom) bakes)
# imported through the owner's route: the chain's per-object level (shadows, stock PBR)
# steps down over 30-500 m, comes back to 0 and holds across a band edge; the Levels view
# paints the CUT's depth, coarser further out; and a slow zoom shows no pop (no band). The fixture is WRITTEN by test_mesh_bake before the run (a setup test), so no
# mesh is checked in. An app run on the rig's display, like atom.view.
set(_lod_switch_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-atom_lod_switch)
set(LOD_STANDIN ${_lod_switch_home}/lod_standin.obj)

# atom.pass_table — THE FULLEST PASS FITS THE PASS-TEXTURE TABLE (BUGS-1 item 1; preflight-atom-3 §A): an Epic world on
# the Basic template with sun contact, the 2D clouds and the World fog (+ one area light created and removed: the LTC
# array then holds a slot in every PBS-family pass) — no engine error, Atom pixels > 0, and every pixel shader the run
# generates (JAHSHAKA_HLMS_DEBUG_DIR, latched at boot: a row of its own) numbers its last texture under the fork's
# NUM_BIND_TEXTURES, read here from the fork's own header. RED at the base (fork fdb715461's 64: the HlmsAtom passes
# asked for 65/66 and VulkanRootLayout refused them). TIER document: the script picks the Epic World Mode itself, the
# measured reproducer exactly (spikes/engine-polish-photon-1/stage1/).
set(_pt_header "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next/RenderSystems/Vulkan/include/OgreVulkanGlobalBindingTable.h")
file(STRINGS "${_pt_header}" _pt_line REGEX "^#define NUM_BIND_TEXTURES [0-9]+u?$")
string(REGEX REPLACE "^#define NUM_BIND_TEXTURES ([0-9]+)u?$" "\\1" _pt_table "${_pt_line}")
if(NOT _pt_table MATCHES "^[0-9]+$")
    message(FATAL_ERROR "atom.pass_table: no NUM_BIND_TEXTURES in ${_pt_header}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_pt_header}")
set(_pt_home ${CMAKE_CURRENT_BINARY_DIR}/pass-table-home)
file(MAKE_DIRECTORY ${_pt_home}/run)
jah_gpu_row(atom.pass_table SHIP CLASS app TIER document FRAMES 552
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/pass_table.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/atom_pass_table.js" "${_pt_table}"
         WORKING_DIRECTORY ${_pt_home}/run)
# TIMEOUT = 6x the measured 42.9 s (solo, the cold shader cache of a fresh fork pin: the boot's compile is most of it).
set_tests_properties(atom.pass_table PROPERTIES
    TIMEOUT 258 LABELS "scripting"
    ENVIRONMENT "HOME=${_pt_home};JAHSHAKA_DATA_ROOT=${_pt_home}/.local/share/Jahshaka")
jah_fresh_home_fixture(atom.pass_table.fresh_home atomPassTableHome "${_pt_home}" WARM LABELS "scripting")
set_property(TEST atom.pass_table APPEND PROPERTY FIXTURES_REQUIRED atomPassTableHome)

set_source_files_properties(test_atom_hidden_cut.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()

# atom.density_dolly — ATOM-5 stage 3: THE DENSITY CAP, TRIANGLES AGAINST DISTANCE. A generated ROUGH facade (a 1 m
# panel of 8,192 triangles, every vertex displaced +-5 cm: relief deeper than its triangles are wide, an error that
# hardly grows per halving) and a smooth 8k sphere beside it, dollied 6 .. 96 m (five stops a factor 2 apart, 30
# frames of settle each) at High with the cap off (1) and on
# (2) and at Epic (1): the device's verdicts are Types.h clusterGroupVerdict's, the triangles never rise, the cap
# coarsens the facade and leaves the sphere exactly as it was, Epic is exact; the exponents and the silhouette delta
# printed. The spec's own row: archived at the merge. It links the bake (IrisGL) and the mirror's hand-off.
add_executable(test_atom_density_dolly test_atom_density_dolly.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cluster_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_source_files_properties(test_atom_density_dolly.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
jah_atom_engine_target(test_atom_density_dolly)
target_include_directories(test_atom_density_dolly PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_atom_density_dolly PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    CLUSTER_FIXTURE_DIR="${_cluster_fixture_dir}")
target_link_libraries(test_atom_density_dolly PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
jah_gpu_row(atom.density_dolly CLASS engine COMMAND test_atom_density_dolly
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.density_dolly PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60 LABELS "engine")
