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

# PICKING vs THE WIDE-ASPECT FOV CLAMP (2026-09-07). Unlike the suite above this
# one needs PIXELS — it renders the scene through the engine at the disputed
# aspect and picks at the centroid the render actually produced — so it links
# JahshakaEngine as well as IrisGL and runs like every other engine suite
# (reachable DISPLAY, Vulkan driver or lavapipe). No jah_no_display here for
# exactly that reason.
add_executable(test_pick_fov_cap test_pick_fov_cap.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scenepicker.cpp)
set_target_properties(test_pick_fov_cap PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_pick_fov_cap PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_pick_fov_cap PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_pick_fov_cap PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_pick_fov_cap JahshakaEngineMedia)
add_test(NAME picking.fov_cap COMMAND test_pick_fov_cap WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(picking.fov_cap PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "engine")

# The leak lane (scripts/sanitize.sh lsan): document picking allocates the
# triangle/BVH scratch per pick.
# QUARANTINED (L2 LightNode::shadowMap — 12 bytes, the suite's only leak).
jah_lsan_blocked(picking.document)
