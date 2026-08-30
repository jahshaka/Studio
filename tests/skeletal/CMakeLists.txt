# Skeletal animation phase (a): CPU skinning. Document advance (GL-free
# Skeleton::boneTransforms), the mirror's per-frame skinning, and the engine's
# dynamic-vertex-buffer verb — all pixel-asserted on a two-bone programmatic arm.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_skeletal_animation test_skeletal_animation.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/scenemirror.cpp)
set_target_properties(test_skeletal_animation PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_skeletal_animation PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_skeletal_animation PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_skeletal_animation PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_skeletal_animation JahshakaEngineMedia)
add_test(NAME skeletal.cpu_skinning COMMAND test_skeletal_animation WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(skeletal.cpu_skinning PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
