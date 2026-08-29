# Characterisation tests for the iris:: scene DOCUMENT (no GL required).
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_document_no_gl test_document_no_gl.cpp)
set_target_properties(test_document_no_gl PROPERTIES AUTOMOC OFF)
target_include_directories(test_document_no_gl PRIVATE
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl/src
    # iris headers expose assimp and bullet types; these are IrisGL's own include paths
    ${CMAKE_SOURCE_DIR}/irisgl/src/assimp/include ${CMAKE_BINARY_DIR}/irisgl/src/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/src/bullet3/src)
target_link_libraries(test_document_no_gl PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
add_test(NAME document.no_gl COMMAND test_document_no_gl
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.no_gl PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document")
