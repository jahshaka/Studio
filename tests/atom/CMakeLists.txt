
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ---------------------------------------------------------------------------
# THE CLUSTER CUT'S CPU LAWS (ATOM-TESTS-BACK-1 stage 1; test_atom_cut_laws.cpp says each law): one verb a row, 0 frames,
# no display, no card - the harness meshes (tests/support/clustermesh.h, proceduralshell.h; no sample model) baked by the
# product's bake, handed across by the product's SceneMirror::toMeshData, cut by Types.h's clusterCut (the rule
# engine.lod_rule_parity holds the device to). The scenemirror and the engine library are the hand-off's, never a frame.
add_executable(test_atom_cut_laws test_atom_cut_laws.cpp cluster_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_atom_cut_laws PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_atom_cut_laws PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_atom_cut_laws PRIVATE IrisGL Qt6::Gui)
# (the independent measure's branch and bound, tests/support/surfacedistance.h, is the no-pop rows' CPU half: optimised, so
# the rows keep the CPU row's 10 s budget in a Debug tree - test_gi_mirror_closed_form's precedent)
target_compile_options(test_atom_cut_laws PRIVATE -O2)
# (measure_agrees reads the bake's tightness literal from its source: irisgl/import/atomdag/measure.cpp)
target_compile_definitions(test_atom_cut_laws PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
jah_document_graph(test_atom_cut_laws)
# atom.cut_closed.multi_mesh is RED by design (SPECS/OPEN_REDS.md, homed at plan 3.5: one DAG a mesh never locks its open
# border, so the split sphere's seam cracks from t 2.465 px; its first run: 38 of 64 t, the worst 84 unmatched edges).
# atom.no_pop.corner and .every_tier are RED by design (SPECS/OPEN_REDS.md, homed at plan 3.4: the rule budgets the group's
# NEAREST point; the corner's 1 + tan^2 theta and a tier's own tolerance pass the owner's 1 px).
foreach(_row cut_partition cut_closed cut_closed.multi_mesh switch_bracket
        no_pop.centre no_pop.corner no_pop.every_tier
        # the bake's step against the independent measure, split BY FIXTURE (each under the CPU row's 10 s; every group)
        no_pop.measure_agrees.closed_sphere no_pop.measure_agrees.round_bar no_pop.measure_agrees.shell_4936)
    add_test(NAME atom.${_row} COMMAND test_atom_cut_laws atom.${_row} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(atom.${_row} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "atom" JAHSHAKA_TEST_FRAMES 0)
    jah_no_display(atom.${_row})
endforeach()

# atom.no_pop.picture (ATOM-TESTS-BACK-1 stage 2; test_atom_no_pop_picture.cpp says the law): ONE card row confirming the
# CPU sweep - one S1 bracket of the closed sphere in two views of one scene (centre, corner), t = 1 -+ 1e-3: the changed ids
# name only the group's clusters, every coverage change within ceil(the CPU's switch_px) of a pixel covered on both. The
# measure's CPU half optimised as the cut laws' is (the same -O2 reason).
add_executable(test_atom_no_pop_picture test_atom_no_pop_picture.cpp cluster_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_atom_no_pop_picture PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_atom_no_pop_picture PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_atom_no_pop_picture PRIVATE IrisGL Qt6::Gui)
target_compile_options(test_atom_no_pop_picture PRIVATE -O2)
jah_document_graph(test_atom_no_pop_picture)
jah_gpu_row(atom.no_pop.picture CLASS engine FRAMES 16 COMMAND test_atom_no_pop_picture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.no_pop.picture PROPERTIES TIMEOUT 60 LABELS "engine" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# THE ENGINE-REACHING TARGETS: a row that reaches past the public Engine API
# (EnginePrivate.h, HlmsAtom.h, AtomPass.h) and links Ogre, building its own
# workspaces on the scene's manager.
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

# atom.component (ATOM-TESTS-BACK-1 stage 3: the plain twin back; test_atom_component.cpp says the law) — Atom is a component
# of its scene: the pass order per view, the words step once a frame, each scene its own caster state, owners retiring with
# their objects. Counts only.
add_executable(test_atom_component test_atom_component.cpp)
jah_atom_engine_target(test_atom_component)
jah_gpu_row(atom.component CLASS engine FRAMES 37 COMMAND test_atom_component
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.component PROPERTIES TIMEOUT 60 LABELS "engine")

# atom.component_asan — test_atom_component.cpp UNDER THE SANITIZER: the owners' retire windows (a
# destroyed view's stats ring and a destroyed scene's caster ring, reaped 8 frames later through the
# device's window) are raw Vulkan memory handed between owners — the use-after-free class a sanitizer is for.
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
    jah_gpu_row(atom.component_asan CLASS engine COMMAND test_atom_component_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # a sanitizer twin whose laws are its plain row's: LONG and a feature end its diff reaches, never FAST (CODING_RULES § 5)
    set_tests_properties(atom.component_asan PROPERTIES TIMEOUT 60 LABELS "engine;long"
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    set_tests_properties(atom.component_asan PROPERTIES JAHSHAKA_TEST_FRAMES 37)   # its plain twin's
endif()

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
jah_gpu_row(atom.pass_table WARM_HOME "${_pt_home}" SHIP CLASS app TIER document FRAMES 552
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/pass_table.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/atom_pass_table.js" "${_pt_table}"
         WORKING_DIRECTORY ${_pt_home}/run)
# TIMEOUT = 6x the measured 42.9 s (solo, the cold shader cache of a fresh fork pin: the boot's compile is most of it).
set_tests_properties(atom.pass_table PROPERTIES
    TIMEOUT 60 LABELS "scripting"
    ENVIRONMENT "HOME=${_pt_home};JAHSHAKA_DATA_ROOT=${_pt_home}/.local/share/Jahshaka")
jah_fresh_home_fixture(atom.pass_table.fresh_home atomPassTableHome "${_pt_home}" WARM LABELS "scripting")
set_property(TEST atom.pass_table APPEND PROPERTY FIXTURES_REQUIRED atomPassTableHome)

set_source_files_properties(test_atom_hidden_cut.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
# atom.hidden_cut — THE ID PASS CULLS WHAT A STOCK WALL HIDES (ATOM-4 stage 0; RE-REGISTERED by ATOM-8 stage 1, the law every
# occlusion stage is held to): a front-culled (stock) wall in front of 30 Atom spheres it hides wholly, three lockstep
# worlds (the occlusion, the same chain with no history, the door shut), both chain shapes (no prepass / the SSR prepass):
# the FIRST list's and the LATE list's draws behind the wall are 0, the depth test rejects the whole field, the ids the
# picture shows equal the history-less twin's word for word and the picture the door-shut world's byte for byte. ATOM-8's
# base number for the occlusion rows, measured by its stage 0 instruments: 53.8 % of the Bistro Exterior law88 frame
# (98,318 of 182,807 triangles at High, 924 x 565) is drawn for objects with no visible pixel.
# atom.receiver_two_sizes (COLD-BOOT-1 stage 2's owed row, built by SHADER-SYSTEM-1 stage 3F (e); the lead's ruling P1) —
# NO PERMUTATION OF THE RECEIVER GRID IS KEYED ON A VIEW'S SIZE: one Atom view with shadows drawn at 128 x 128 and at
# 320 x 96, the receiver depth job's properties hold no group count at either size (one permutation); its negative
# control, the job told its sizes (the base's state), keys two. Frames, never time (48). A new row: priced from its solo
# run at the lane's feature end, never SHIP.
add_executable(test_atom_receiver_two_sizes test_atom_receiver_two_sizes.cpp)
jah_atom_engine_target(test_atom_receiver_two_sizes)
jah_gpu_row(atom.receiver_two_sizes CLASS engine FRAMES 48 COMMAND test_atom_receiver_two_sizes
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.receiver_two_sizes PROPERTIES TIMEOUT 60 LABELS "engine")

add_executable(test_atom_hidden_cut test_atom_hidden_cut.cpp)
jah_atom_engine_target(test_atom_hidden_cut)
jah_gpu_row(atom.hidden_cut CLASS engine FRAMES 33 COMMAND test_atom_hidden_cut
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.hidden_cut PROPERTIES TIMEOUT 60 LABELS "engine")

# atom.cluster_occlusion (ATOM-8 stage 2; the builder's row) — THE ID PASS CULLS HIDDEN CLUSTERS: a front Atom facade hiding a
# taller back one whose box overlaps the front's (Bistro's two measured classes: the sky over the roofline, the kit-bash
# overlap), three lockstep worlds (the occlusion, no history, the door shut): at rest the first list draws of the back facade
# only its sky-class clusters; on every reveal (the camera teleported past the front facade's edge) the late pass draws the
# rejected clusters and the picture equals the door-shut world's byte for byte on that very frame; ids word-equal; no
# overflow. The base (engine.arm atom.clusterOcclusion 0, the objects' own test) is printed: the whole back facade drawn.
# The base number of the occlusion rows (ATOM-8 stage 0): 53.8 % of Bistro Exterior's law88 frame was drawn for objects
# with no visible pixel.
add_executable(test_atom_cluster_occlusion test_atom_cluster_occlusion.cpp)
set_source_files_properties(test_atom_cluster_occlusion.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
jah_atom_engine_target(test_atom_cluster_occlusion)
jah_gpu_row(atom.cluster_occlusion CLASS engine FRAMES 145 COMMAND test_atom_cluster_occlusion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.cluster_occlusion PROPERTIES TIMEOUT 60 LABELS "engine")

# atom.decode_parity.{materials,normal_map,point,spot,area} (ATOM-TESTS-BACK-1 stage 3; test_atom_decode_parity.cpp says the
# law and the class) - THE DECODE IS STOCK PBS: one scene's cells with the visibility buffer's door open (the decode) and shut
# (stock PBS), in linear radiance |decode - stock| <= eps G + q outside "a pixel whose 3 x 3 id-image neighbourhood holds
# another word" (eps 1/256 px, the decode's sub-pixel snap; q one fp16 quantum of the target); GI off, route `none` declared
# on frame 1, shadows off (a NAMED GAP: the decode's shadow receive path waits for the caster cut's item). One row per
# proof (the lead, 2026-11-22): every row builds the same scene and counts its own cells.
# atom.decode_parity.normal_map and .area are RED by design (SPECS/OPEN_REDS.md, homed at plan 3.6, causes unexplained):
# the normal-mapped cell's same four pixels over the bound under every lamp (worst 1.92 q, 12.2 eps); the area lamp's 422 px
# (worst 24.54 q, 128 eps at (589, 169)).
add_executable(test_atom_decode_parity test_atom_decode_parity.cpp)
jah_atom_engine_target(test_atom_decode_parity)
# FRAMES: 4 to boot the scene + 8 an arm (two pictures, each still at its second read 2 frames apart: the first run's 4 / 4).
foreach(_arm_frames materials=12 normal_map=28 point=12 spot=12 area=12)
    string(REPLACE "=" ";" _af "${_arm_frames}")
    list(GET _af 0 _arm)
    list(GET _af 1 _frames)
    jah_gpu_row(atom.decode_parity.${_arm} CLASS engine FRAMES ${_frames} COMMAND test_atom_decode_parity ${_arm}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(atom.decode_parity.${_arm} PROPERTIES TIMEOUT 60 LABELS "engine")
endforeach()

# atom.masked_coverage (RE-REGISTERED by ATOM-TESTS-BACK-1 stage 3; its device read, AlphaMaskStore::readBits, restored in
# irisgl by the same stage) — THE MASK'S
# PYRAMID KEEPS LEVEL 0'S COVERAGE: the device's pyramid read back against bit_i = round(S_i) - round(S_(i-1)) level by level
# (a 1-texel checker bit for bit; a non-power-of-two hash pattern), and the id pass's coverage of the checker at 1x / 2x / 4x /
# 8x minification = level 0's 1/2 within one texel (DOCS/traps/engine/atom-and-lod.md, CUT-OUTS).
add_executable(test_atom_masked_coverage test_atom_masked_coverage.cpp)
jah_atom_engine_target(test_atom_masked_coverage)
jah_gpu_row(atom.masked_coverage CLASS engine FRAMES 88 COMMAND test_atom_masked_coverage
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# atom.masked_coverage.clamp - the mask's coverage equals the material's own alpha test, its sampler's address mode included,
# at every texel centre of a CLAMP quad with UVs to 1.5. RED by design (SPECS/OPEN_REDS.md, homed at plan 3.6): the mask
# wraps (jah_alpha_mask.glsl:95) where the slot clamps - covered 0.33333 against the law's 0.66667, 192 of 576 cells.
jah_gpu_row(atom.masked_coverage.clamp CLASS engine FRAMES 28 COMMAND test_atom_masked_coverage clamp
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.masked_coverage atom.masked_coverage.clamp PROPERTIES TIMEOUT 60 LABELS "engine")

# atom.cut_overflow_whole / atom.still_does_nothing (ATOM-TESTS-BACK-1 stage 4; test_atom_cut_overflow.cpp says the laws) —
# COUNTS, NEVER MS: the test door at 1/8 of the scene's ask draws every instance whole (coarse from the reserve, nothing
# missing), the budget grows ONCE to its closed form and the overflow is read on at most F + 2 frames (F the frames in flight:
# the read-back's latency, derived in the source); a still frame visits no word slot and re-sizes no cut. The closed sphere baked by the product's bake (cluster_fixtures), the engine's private read of F.
add_executable(test_atom_cut_overflow test_atom_cut_overflow.cpp cluster_fixtures.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
jah_atom_engine_target(test_atom_cut_overflow)
target_include_directories(test_atom_cut_overflow PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_atom_cut_overflow PRIVATE IrisGL Qt6::Gui)
set_source_files_properties(test_atom_cut_overflow.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
jah_document_graph(test_atom_cut_overflow)
jah_gpu_row(atom.cut_overflow_whole CLASS engine FRAMES 22 COMMAND test_atom_cut_overflow overflow
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
jah_gpu_row(atom.still_does_nothing CLASS engine FRAMES 26 COMMAND test_atom_cut_overflow still
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.cut_overflow_whole atom.still_does_nothing PROPERTIES TIMEOUT 60 LABELS "engine"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
