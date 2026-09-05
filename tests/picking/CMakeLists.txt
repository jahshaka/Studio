# Document picking, no renderer.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_scene_picker test_scene_picker.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scenepicker.cpp)
set_target_properties(test_scene_picker PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17)
target_include_directories(test_scene_picker PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_scene_picker PRIVATE JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_scene_picker PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_scene_picker)
add_test(NAME picking.document COMMAND test_scene_picker WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(picking.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document")
jah_no_display(picking.document)

# The leak lane (scripts/sanitize.sh lsan): document picking allocates the
# triangle/BVH scratch per pick.
# QUARANTINED (L2 LightNode::shadowMap — 12 bytes, the suite's only leak).
jah_lsan_blocked(picking.document)
