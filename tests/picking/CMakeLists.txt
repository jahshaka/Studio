# Document picking, no renderer.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_scene_picker test_scene_picker.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scenepicker.cpp)
set_target_properties(test_scene_picker PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17)
target_include_directories(test_scene_picker PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_scene_picker PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
add_test(NAME picking.document COMMAND test_scene_picker WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(picking.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document")
