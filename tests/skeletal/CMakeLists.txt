# Skeletal animation phase (a): CPU skinning. Document advance (GL-free
# Skeleton::boneTransforms), the mirror's per-frame skinning, and the engine's
# dynamic-vertex-buffer verb — all pixel-asserted on a two-bone programmatic arm.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ANIMATION_ENGINE_MIGRATION_SPEC gates G2-G6: the clip boundary. Same rig, same
# clip, same absolute times, driven through the document evaluator and through
# Ogre's SkeletonAnimation, compared at the bone matrices the vertex shader is
# handed AND at the pixels — plus per-bone weight normalization, the manual-bone
# override rule, determinism, and the FBX pivot rig end to end. This is the
# comparison that has to exist before the document evaluator is deleted.
add_executable(test_gpu_clips test_gpu_clips.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_gpu_clips PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gpu_clips PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gpu_clips PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gpu_clips PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_gpu_clips JahshakaEngineMedia)
jah_gpu_row(skeletal.gpu_clips SHIP CLASS engine COMMAND test_gpu_clips WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(skeletal.gpu_clips PROPERTIES JAHSHAKA_TEST_FRAMES 66)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(skeletal.gpu_clips PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "mirror")

# AVATAR_RIG_PERF_SPEC P1b: skeleton SHARING and its lifetime rules (§3.2/§3.3),
# which is also the spike the spec asked for before P1b was estimated — kept as
# a gate instead of thrown away. Engine only (no document, no Qt), on the NULL
# render system: no display, no GPU. Built a SECOND time against the sanitised
# backend when JAHSHAKA_ASAN is on, because every trap here is a dangling
# pointer and ASan is the instrument that names them.
function(jahshaka_share_test target)
    add_executable(${target} test_skeleton_share.cpp)
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
endfunction()

if(JAHSHAKA_ASAN)
    jahshaka_share_test(test_skeleton_share_asan)
    target_link_libraries(test_skeleton_share_asan PRIVATE JahshakaEngineAsan)
    add_test(NAME skeletal.share_asan COMMAND test_skeleton_share_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    jah_ship_row(skeletal.share_asan)   # THE SHIP SET (TEST-CUT-1)
    # LeakSanitizer off for the same reason tests/engine gives: the only leaks
    # are Ogre's own, and this suite is here for use-after-free, not for leaks.
    # An ASAN twin runs at the stage close (TESTING-SPEED-B4): skeletal.share (archived: archive/tests/skeletal) is the merge row.
    set_tests_properties(skeletal.share_asan PROPERTIES TIMEOUT 60 LABELS "engine"
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    jah_no_display(skeletal.share_asan)
endif()
