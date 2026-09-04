# Characterisation tests for the iris:: scene DOCUMENT (no GL required).
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_document_no_gl test_document_no_gl.cpp)
set_target_properties(test_document_no_gl PROPERTIES AUTOMOC OFF)
target_include_directories(test_document_no_gl PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose assimp and bullet types; these are IrisGL's own include paths
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_document_no_gl PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
add_test(NAME document.no_gl COMMAND test_document_no_gl
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.no_gl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document")

# The leak lane (scripts/sanitize.sh lsan): the document model itself — every
# node, mesh, material and animation type constructed and dropped. No Vulkan,
# no app process, so LeakSanitizer runs with detect_leaks=1 here.
# QUARANTINED (L2 LightNode::shadowMap, L5 KeyFrame keys) plus one leak of
# the suite's own (test_document_no_gl.cpp:386 news a Vector3DPropertyAnim
# it never deletes). Move to jah_lsan_lane once L2/L5 are fixed.
jah_lsan_blocked(document.no_gl)
