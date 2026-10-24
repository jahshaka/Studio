# Document picking, no renderer.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_pick_fov_cap PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_pick_fov_cap PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_pick_fov_cap JahshakaEngineMedia)
jah_gpu_row(picking.fov_cap SHIP CLASS engine COMMAND test_pick_fov_cap WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(picking.fov_cap PROPERTIES JAHSHAKA_TEST_FRAMES 9)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(picking.fov_cap PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 60 LABELS "engine")

