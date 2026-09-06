# Avatar module, Part 0 (AVATAR_MODULE_SPEC).
#
# avatar.document — ITEM ZERO (a skinned SINGLE-MESH file must get bone scene
#   nodes and a moving pose; pre-fix that was 0 of N bones, silently) plus the
#   engine-free AvatarPreviewModel the `avatar` verbs drive: clips and their
#   display names, transport, the two toggles, the bone tree. No engine at all.
# avatar.preview — the same model through AvatarPreviewScene + BoneOverlay into
#   an offscreen View: the four (mesh, skeleton) combinations, pixel-asserted.
#
# The rigged fixture is GENERATED, not downloaded: fixtures/make_rig_glb.py
# writes a ~3 KB two-bone skinned GLB with two named clips, one of them called
# "mixamo.com" so the display-name rule is exercised by a real file. Same for
# the MOCAP fixtures: fixtures/make_bvh_fixtures.py writes two plain-ASCII
# .bvh files (one on rig2.glb's joint names, one on a foreign rig) for the
# avatar.document B-sections and avatar.preview's S10 — the .bvh importer only
# exists because irisgl/CMakeLists.txt names ASSIMP_BUILD_BVH_IMPORTER in the
# assimp allowlist, and those sections are what keeps that option honest.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Sql Multimedia)

# beta_mannequin.dae — the DEFAULT AVATAR fixture: Mixamo's Beta character
# (full standard Mixamo skeleton, bare joint names) with its baked dance clip
# stripped so the body is neutral. GENERATED here rather than checked in: the
# ~5 MB of bytes already ship inside scenes/"Skeletal Animation.zip", and
# fixtures/make_beta_mannequin.py derives the clipless .dae from that archive.
# Consumed by scripting.e2e.mannequin (path substituted over there) and by
# anyone who wants a real humanoid to spawn.
set(JAHSHAKA_MANNEQUIN_DAE "${CMAKE_CURRENT_BINARY_DIR}/fixtures/beta_mannequin.dae"
    CACHE INTERNAL "generated animation-free Mixamo Beta avatar fixture")
add_custom_command(OUTPUT "${JAHSHAKA_MANNEQUIN_DAE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_beta_mannequin.py"
            "${CMAKE_SOURCE_DIR}" "${JAHSHAKA_MANNEQUIN_DAE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/make_beta_mannequin.py"
            "${CMAKE_SOURCE_DIR}/scenes/Skeletal Animation.zip"
    COMMENT "Deriving beta_mannequin.dae (animation-free Mixamo Beta) from the sample archive"
    VERBATIM)
add_custom_target(beta_mannequin ALL DEPENDS "${JAHSHAKA_MANNEQUIN_DAE}")

# assethelper.cpp's link closure — the module's load route (D0.1 A) goes
# through it. Same set tests/assetmeta links.
set(AVATAR_LOAD_SOURCES
    ${CMAKE_SOURCE_DIR}/src/modules/avatar/avatarpreviewmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/avatar/avatarspace.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)

add_executable(test_avatar_document test_avatar_document.cpp ${AVATAR_LOAD_SOURCES})
set_target_properties(test_avatar_document PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_avatar_document PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_document PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_avatar_document PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_avatar_document)
add_test(NAME avatar.document COMMAND test_avatar_document WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "avatar")
jah_no_display(avatar.document)

add_executable(test_avatar_preview test_avatar_preview.cpp ${AVATAR_LOAD_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/bridge/avatarpreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/boneoverlay.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_avatar_preview PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_avatar_preview PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_preview PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_avatar_preview PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_dependencies(test_avatar_preview JahshakaEngineMedia)
add_test(NAME avatar.preview COMMAND test_avatar_preview WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "avatar")

# avatar.movement — AVATAR_LOCOMOTION_SPEC Stage 2, gates M1-M7.
#
# The movement component (§6) through the REAL seam: iris::Scene ->
# Environment::stepSimulation -> updateAvatarMovement, with static colliders
# built by the production PhysicsHelper path. No engine rendering, no display:
# the document graph's NULL render system is the only engine here, and
# jah_no_display below proves it.
#
# It repays the `player.lifecycle` suite Stage 0 removed: M7 asserts the same
# lifecycle properties (delete mid-play, no stale broadphase entry, no double
# free) against the component that replaced the 2016 controller.
add_executable(test_avatar_movement test_avatar_movement.cpp)
set_target_properties(test_avatar_movement PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_avatar_movement PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_movement PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_avatar_movement PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_avatar_movement)
add_test(NAME avatar.movement COMMAND test_avatar_movement
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.movement PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300 LABELS "avatar")
jah_no_display(avatar.movement)

# avatar.possession — AVATAR_LOCOMOTION_SPEC Stage 3, gates P1-P6.
#
# Possession, the camera-relative routing it decides, the spring-arm follow
# camera and the `playMode` world setting, through the same real seam
# avatar.movement uses: iris::Scene -> setPlaying (the possession EDGE) ->
# Scene::update -> Environment::updateAvatarMovement. Every gate writes the
# gameplay INPUT STATE (iris::InputSystem, the struct `avatar.input` and the
# keyboard producer both write) rather than poking a component, which is what
# makes the headless and the live path one code path.
#
# No engine rendering, no display: the document graph's NULL render system is
# the only engine here, and jah_no_display below proves it.
add_executable(test_avatar_possession test_avatar_possession.cpp)
set_target_properties(test_avatar_possession PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_avatar_possession PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_possession PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_avatar_possession PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_avatar_possession)
add_test(NAME avatar.possession COMMAND test_avatar_possession
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.possession PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300 LABELS "avatar")
jah_no_display(avatar.possession)

# avatar.locomotion — AVATAR_LOCOMOTION_SPEC Stage 4 (S1-S7) and the
# document-side half of Stage 5 (S8, S9, S10, S12).
#
# The locomotion state machine (§7): the closed condition vocabulary and its
# parser, first-match-wins evaluation, the cross-fade clock, clip roles and
# their tolerant name matching, the shipped "Biped Locomotion" default asset and
# every degradation of it, the JSON grammar the scene file and the verbs share,
# and determinism under a fixed dt.
#
# Driven through the SAME real seam avatar.movement uses — iris::Scene ->
# Environment::stepSimulation -> updateAvatarMovement -> the state machine
# stepped immediately after the movement that published its inputs — except
# where a gate is pure arithmetic (S2's blend clock), which is driven directly
# with a synthetic §5 contract and says so at the section.
#
# No engine rendering, no display: the document graph's NULL render system is
# the only engine here, and jah_no_display below proves it.
add_executable(test_avatar_locomotion test_avatar_locomotion.cpp)
set_target_properties(test_avatar_locomotion PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_avatar_locomotion PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_locomotion PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_avatar_locomotion PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_avatar_locomotion)
add_test(NAME avatar.locomotion COMMAND test_avatar_locomotion
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.locomotion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 300 LABELS "avatar")
jah_no_display(avatar.locomotion)

# avatar.locomotion_mirror — AVATAR_LOCOMOTION_SPEC Stage 5, gates S11 and S13.
#
# The two Stage 5 gates that are claims about the ENGINE rather than about the
# document, so they need one: S11 (clipBoneWeights reports the per-bone
# normalization the engine applied to the blend space's raw intent, arriving
# through SceneMirror::syncClips rather than through a hand-written ClipState
# array) and S13 (an editor<->player page switch mid-walk — two SceneMirrors on
# two engine Scenes sharing ONE document, driving the real graph-ownership
# evacuation protocol, with the phase, the published weights and the per-bone
# result asserted across both swaps). It also measures R13's rebuild cost, which
# the spec's §14 records as reasoned and never run.
#
# Links IrisGL AND JahshakaEngine, like tests/mirror: it spans the boundary on
# purpose. Offscreen view, no window; DISPLAY must be reachable (Vulkan), so it
# gets NO jah_no_display.
add_executable(test_avatar_locomotion_mirror test_avatar_locomotion_mirror.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_avatar_locomotion_mirror PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_avatar_locomotion_mirror PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_locomotion_mirror PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_avatar_locomotion_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_avatar_locomotion_mirror JahshakaEngineMedia)
add_test(NAME avatar.locomotion_mirror COMMAND test_avatar_locomotion_mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.locomotion_mirror PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "avatar")
