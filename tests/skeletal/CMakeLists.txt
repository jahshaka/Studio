# Skeletal animation phase (a): CPU skinning. Document advance (GL-free
# Skeleton::boneTransforms), the mirror's per-frame skinning, and the engine's
# dynamic-vertex-buffer verb — all pixel-asserted on a two-bone programmatic arm.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_skeletal_animation test_skeletal_animation.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_skeletal_animation PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_skeletal_animation PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_skeletal_animation PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_skeletal_animation PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_skeletal_animation JahshakaEngineMedia)
add_test(NAME skeletal.cpu_skinning COMMAND test_skeletal_animation WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(skeletal.cpu_skinning PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")

# GPU_SKINNING_SPEC phase 1 / T4: per-node pose ownership, document-only (no
# engine, no window). Two MeshNodes on ONE mesh asset must hold two poses.
add_executable(test_multi_avatar test_multi_avatar.cpp)
set_target_properties(test_multi_avatar PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_multi_avatar PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_multi_avatar PRIVATE IrisGL Qt6::Gui)
add_test(NAME skeletal.multi_avatar COMMAND test_multi_avatar WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(skeletal.multi_avatar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "document")

# GPU_SKINNING_SPEC phase 2 / T1, T2, T7: the v1-skeleton translation layer,
# proved WITHOUT rendering — R1 (bind-pose reconciliation) is the one place this
# program fails if it fails, and its symptom is not debuggable by eye.
add_executable(test_rig_translation test_rig_translation.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_rig_translation PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_rig_translation PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_rig_translation PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_rig_translation PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_rig_translation JahshakaEngineMedia)
add_test(NAME skeletal.rig_translation COMMAND test_rig_translation WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(skeletal.rig_translation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
