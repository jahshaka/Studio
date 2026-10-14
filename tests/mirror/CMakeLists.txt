# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
# SceneMirror tests: iris:: document -> engine, verified by pixels. Links IrisGL
# (Qt) AND JahshakaEngine — the only suite that spans the boundary, on purpose.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_scene_mirror test_scene_mirror.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_scene_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_scene_mirror PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_scene_mirror PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_scene_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_scene_mirror JahshakaEngineMedia)
jah_gpu_row(mirror.document_to_engine CLASS engine COMMAND test_scene_mirror WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.document_to_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.document_to_engine PROPERTIES JAHSHAKA_TEST_FRAMES 856)

# mirror.skinned_outline — the SELECTION SILHOUETTE over a skinned character.
# The shell is a second Item over the same mesh; on HlmsUnlit (which has no
# skeletal path) it drew the BIND POSE forever, so an animating character grew a
# solid selection-coloured twin that read as "the character renders twice".
# Pixel gate: with the character bent 90 degrees, the space its bind pose
# occupied is empty. Plus one-item-per-node, stopped and playing.
add_executable(test_skinned_outline test_skinned_outline.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_skinned_outline PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_skinned_outline PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_skinned_outline PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_skinned_outline PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_skinned_outline JahshakaEngineMedia)
jah_gpu_row(mirror.skinned_outline CLASS engine COMMAND test_skinned_outline WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.skinned_outline PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.skinned_outline PROPERTIES JAHSHAKA_TEST_FRAMES 9)

# mirror.cull_twin — A NODE'S FACE CULL through its material's CULL TWIN (CULL-MODE-2):
# one datablock per (material, cull) worn, kept in step with the material, gone
# with its last wearer; the plane-from-below and closed-box-from-inside pictures;
# the Atom split sees a shared Back twin as one material.
add_executable(test_cull_twin test_cull_twin.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_cull_twin PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_cull_twin PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_cull_twin PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_cull_twin PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_cull_twin JahshakaEngineMedia)
jah_gpu_row(mirror.cull_twin CLASS engine COMMAND test_cull_twin WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.cull_twin PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.cull_twin PROPERTIES JAHSHAKA_TEST_FRAMES 652)

# mirror.frame_input — THE MIRROR EMITS ONE RECORD A FRAME, AND A STILL ONE DRAINS TO NOTHING (ENGINE-UPGRADE-1 U0
# 3a): FrameInputStatus counts — 20 still frames = 20 submits and drains applying 0 fields; the fog on = 1 field, then 0;
# a camera move = the per-view part only. A law in counts.
add_executable(test_mirror_frame_input test_mirror_frame_input.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_frame_input PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_frame_input PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_frame_input PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_frame_input PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_frame_input JahshakaEngineMedia)
jah_gpu_row(mirror.frame_input CLASS engine FRAMES 34 COMMAND test_mirror_frame_input
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.frame_input PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "mirror")

# mirror.doc_store — THE DOCUMENT'S TRANSFORM STORE IS THE ENGINE'S TRUTH (ENGINE-UPGRADE-U2 stage 1, DOCSTORE): on a
# 1,000-node fixture with non-uniform, mirrored and zero scales, every world from the store equals Ogre's derived
# transform after the frame (1e-6) and every parent / child set equals Ogre's (the order is the store's: case H), after a
# keepTransform reparent, a scale, a rotation, zero-scale parents and a sheared parent; the re-expression keeps the
# world and stores no NaN (SC1-REEXPRESS-SCALE-1). 7 cases x 2 frames. Case H pins A REAL FIX: since SCENEGRAPH D2 the
# document's sibling order WAS Ogre's child vector, and Ogre-Next's removeChild is swap-and-pop, so every reparent or
# delete permuted the remaining siblings (the outliner's order, undo's sibling index, the order a file is saved in);
# the store keeps the document's order.
add_executable(test_doc_store test_doc_store.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_doc_store PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_doc_store PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_doc_store PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_doc_store PRIVATE IrisGL JahshakaEngine OgreNext Qt6::Gui Qt6::OpenGL)   # OgreNext: the engine's cached derived transform, read after a frame
add_dependencies(test_doc_store JahshakaEngineMedia)
jah_gpu_row(mirror.doc_store CLASS engine FRAMES 14 COMMAND test_doc_store
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.doc_store PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "mirror")

# mirror.change_set — THE FRAME'S CHANGE SET IS THE ONE SOURCE OF WHAT MOVED (ENGINE-UPGRADE-1 U0 3b): a still world
# drains empty sets and costs no GI work (no scan, no caster visit, no GPU-table slot); one cube = one entry and one
# caster visit; a group = one entry and its subtree; the lamp = one entry, one light, the serial +1; the soft promotion
# decided by the set (written while playing, never on the play edge). A law in counts.
add_executable(test_mirror_change_set test_mirror_change_set.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_change_set PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_change_set PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_change_set PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_change_set PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_change_set JahshakaEngineMedia)
jah_gpu_row(mirror.change_set CLASS engine FRAMES 242 COMMAND test_mirror_change_set
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.change_set PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "mirror")

# mirror.reparent_local — A REPARENT COSTS THE FRAGMENT, NEVER THE SCENE (ENGINE-UPGRADE-1 stage 2): one 500-node
# fragment moved three ways inside a 1k and a 10k lattice; the same counts at both scales (the mirror's visits, the
# engine's Items, ONE GI invalidation per sync, the static switches, the drawn pose after a move). A law in counts; 4 frames.
add_executable(test_mirror_reparent test_mirror_reparent.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_reparent PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_reparent PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_reparent PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_reparent PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_reparent JahshakaEngineMedia)
jah_gpu_row(mirror.reparent_local CLASS engine FRAMES 4 COMMAND test_mirror_reparent
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.reparent_local PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")

# mirror.scale — the SHAPE of a still frame's mirror cost at scale (MIRROR_SCALE
# lane, 2026-09-13). The render review measured `host.mirror` at 52 ms per STILL
# frame on an 8,404-node lattice; this suite is the regression gate, asserting
# ratios and allocation counts rather than milliseconds (a wall-clock budget on
# a shared box is a flake generator).
add_executable(test_mirror_scale test_mirror_scale.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_scale PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_scale PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_scale PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_scale PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_scale JahshakaEngineMedia)
# THE GPU-TIMING LOCK: it is a TIMING suite (lane D6B-GATE-SHAPE; audit §4 R3 — it was
# RUN_SERIAL, which reaches only inside one ctest). The ratios are load-tolerant by
# construction (every arm pays the same contention), and the lock keeps every other
# measurement on the box out of its window.
jah_gpu_exclusive_test(NAME mirror.scale CLASS engine RUN_TIMEOUT 300 COMMAND test_mirror_scale
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.scale PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "mirror")
set_tests_properties(mirror.scale PROPERTIES JAHSHAKA_TEST_FRAMES 0)

# mirror.settle_incremental — the static re-promotion after a settle is a
# BOUNDED walk spread over syncs (SceneMirror::kStaticSettleNodesPerSync,
# MIRROR-SETTLE-1) and lands exactly the one-shot applyStaticDefaults' answer:
# two identical 50,000-node documents, one settled each way, compared node for
# node (graph class + hint), plus a settle written, mutated and scene-switched
# part-way through, and the demotion worst case. Counts only — the
# milliseconds it prints are for the reader, so it is a plain GPU row.
add_executable(test_mirror_settle test_mirror_settle.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_settle PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_settle PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_settle PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_settle PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_settle JahshakaEngineMedia)
jah_gpu_row(mirror.settle_incremental CLASS engine COMMAND test_mirror_settle
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# TIMEOUT (the policy, tests/CMakeLists.txt): max(6 x quiet median, slot-era worst) = max(6 x 70.24, 71.59) -> 430 s.
# Sample: n=2 quiet PASS runs at B=24 (71.59, 68.89 s; median 70.24, worst 71.59), warm, 2026-10-11 12:4x, quiet box
# (no VRAM wait in these seconds).
set_tests_properties(mirror.settle_incremental PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 430 LABELS "mirror")
set_tests_properties(mirror.settle_incremental PROPERTIES JAHSHAKA_TEST_FRAMES 0)

# mirror.settle_thumbnails — a settle is fed by ITS OWN document's writes:
# a 10k scene demoted by one edit settles within its quiet window + one
# rotation while a thumbnail (a throwaway document: a write and a destruction
# per tile) is rendered on every sync (MIRROR-SETTLE-1 fix round 1). Counts.
# In bin/ beside the thumbnail suites (the renderer resolves its media there).
add_executable(test_mirror_settle_thumbs test_mirror_settle_thumbs.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginepreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/previewenvironment.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginethumbnailrenderer.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp)
set_target_properties(test_mirror_settle_thumbs PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_settle_thumbs PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_settle_thumbs PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_settle_thumbs PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_settle_thumbs JahshakaEngineMedia)
jah_gpu_row(mirror.settle_thumbnails CLASS engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_mirror_settle_thumbs
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# TIMEOUT (the policy): max(6 x 11.12, 11.20) = 67 s -> the 120 s floor. Sample: n=2 quiet PASS runs at B=24
# (11.03, 11.20 s), warm, 2026-10-11 12:4x, quiet box.
set_tests_properties(mirror.settle_thumbnails PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.settle_thumbnails PROPERTIES JAHSHAKA_TEST_FRAMES 894)

# mirror.dirty_equals_full — THE DIFFERENTIAL TEST of the dirty-set mirror
# (SPECS/DIRTY_SET_MIRROR_SPEC.md §7.1). For every class of mutation the
# document supports: make the change, run the DIRTY sync that ships, then run
# the WHOLE WALK behind it as an oracle and demand it pushes nothing. A single
# push is a change the document failed to report — the one way this design can
# fail, and the one no pixel test would catch.
add_executable(test_dirty_mirror test_dirty_mirror.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_dirty_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_dirty_mirror PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_dirty_mirror PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_dirty_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_dirty_mirror JahshakaEngineMedia)
jah_gpu_row(mirror.dirty_equals_full CLASS engine COMMAND test_dirty_mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.dirty_equals_full PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.dirty_equals_full PROPERTIES JAHSHAKA_TEST_FRAMES 0)

# mirror.attach_budget — A FRAME BUILDS ITS SHARE OF NEW MESHES (BISTRO-ENGINE-1
# stage 5, finding 9): SceneMirror::setAttachBudget's vertex and index caps
# against ceil(N*v/V), the streamed world equal to the document (the full walk
# pushes nothing), a built mesh never deferred, an unbudgeted sync unchanged.
add_executable(test_mirror_attach_budget test_mirror_attach_budget.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_attach_budget PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_attach_budget PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_attach_budget PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_attach_budget PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_attach_budget JahshakaEngineMedia)
jah_gpu_row(mirror.attach_budget CLASS engine COMMAND test_mirror_attach_budget
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.attach_budget PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.attach_budget PROPERTIES JAHSHAKA_TEST_FRAMES 0)

# mirror.grid_floor — THE GROUND GRID ON TOP OF THE FLOOR (GIZMO-2 item 5, owner
# §370). The grid sits at exactly y=0 now and wins against the coplanar floor
# through its material's depth bias instead of being pushed underneath it; a box
# standing on the floor still covers it, and a view with the editor's furniture
# off draws none of it. Pixels, through the real mirror and the real engine.
add_executable(test_grid_floor test_grid_floor.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_grid_floor PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_grid_floor PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_grid_floor PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_grid_floor PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_grid_floor JahshakaEngineMedia)
jah_gpu_row(mirror.grid_floor CLASS engine COMMAND test_grid_floor WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.grid_floor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.grid_floor PROPERTIES JAHSHAKA_TEST_FRAMES 42)

# mirror.ground_plane — THE GROUND PLANE WIDGET (WORLD-MODEL-1): the editor's
# infinite matte ground is a mirror-owned backdrop quad, off by default, out of
# every capture (and off Atom), and a hide is not a delete (DRAG-1). Plus the
# Player's floor hide landing on the next sync (PLAYER-FLOOR-1). Pixels plus the
# engine's object census and the plane node's own flags.
add_executable(test_ground_plane test_ground_plane.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_ground_plane PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_ground_plane PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_ground_plane PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_ground_plane PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_ground_plane JahshakaEngineMedia)
jah_gpu_row(mirror.ground_plane CLASS engine COMMAND test_ground_plane
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.ground_plane PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.ground_plane PROPERTIES JAHSHAKA_TEST_FRAMES 99)

# mirror.sun_night — WHEN THE SUN GOES OUT (SKY-SMALL / SKY-NIGHT-1). The disc
# and the sun's PSSM passes used to be dropped on a RELATIVE tint test, which
# with SKY-DENSITY-1's physical transmittance crossed at +3.6 degrees of sun
# elevation at haze 6 and +6.3 at haze 10 — the disc popping off a visibly-up
# sun. The rule is absolute now (one 8-bit output code of radiance, derived in
# scenemirror.cpp); this is the sweep that pins it, per haze, as pixels.
add_executable(test_sun_night test_sun_night.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_sun_night PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_sun_night PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_sun_night PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_sun_night PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_sun_night JahshakaEngineMedia)
jah_gpu_row(mirror.sun_night CLASS engine COMMAND test_sun_night WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.sun_night PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.sun_night PROPERTIES JAHSHAKA_TEST_FRAMES 216)

# mirror.vr_proxies — THE WEARER, DRAWN AT THE DESK AND IN EVERY EYE (VR_SPEC
# §5 phase 4, lane VR-4). The two HAND markers the editor's VR preview shows
# (there is no head marker, by the owner's rule), the two-bit helper rule that
# decides which picture draws which furniture, and the proof that a helper-less
# picture — the Player's desktop window, a thumbnail, a user's screenshot — is
# byte-identical to one with no session at all.
#
# NEEDS NO RUNTIME: setVrProxies takes a VrStatus as data, so the suite hands it
# three poses and never asks OpenXR anything. The runtime half is vr.session.
# THE APP'S OWN MODEL RESOURCES, compiled into this suite (claim H, phase 4b
# stage 1): the mirror's default controller-model path is a qrc path
# (":/content/vr/meta-quest-touch-pro/left.obj"), so the suite that asserts the
# model is drawn must carry the same resource the app ships or it would be
# testing a path the product does not use.
qt6_add_resources(_vr_proxies_qrc ${CMAKE_SOURCE_DIR}/app/models.qrc)
add_executable(test_vr_proxies test_vr_proxies.cpp ${_vr_proxies_qrc}
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_vr_proxies PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_vr_proxies PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
# (No JAHSHAKA_SOURCE_DIR here: the app's own models.qrc is compiled in above,
# which is the point of claim H — the suite reads the SHIPPED resource path and
# never the source tree. The define was left behind by the first cut and unused.)
target_compile_definitions(test_vr_proxies PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_vr_proxies PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_vr_proxies JahshakaEngineMedia)
jah_gpu_row(mirror.vr_proxies CLASS engine COMMAND test_vr_proxies
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.vr_proxies PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.vr_proxies PROPERTIES JAHSHAKA_TEST_FRAMES 94)
