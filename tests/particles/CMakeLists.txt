# Particle mirroring: engine billboard verbs (BillboardSet2) + the SceneMirror
# branch that pushes document-simulated particles into them, verified by pixels.
# Links IrisGL (Qt) AND JahshakaEngine like the mirror suite.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_particles test_particles.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_particles PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_particles PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_particles PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_particles PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_particles JahshakaEngineMedia)
add_test(NAME particles.document_to_engine COMMAND test_particles WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(particles.document_to_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "particles")

# ParticleFX2 adoption (PARTICLES_FX2_SPEC.md phase 0): the ENGINE's own
# simulation, with no document and no mirror. Links JahshakaEngine only, plus
# Qt6::Gui for the offscreen QGuiApplication the Vulkan/XCB support object needs.
add_executable(test_particles_pfx2 test_particles_pfx2.cpp)
set_target_properties(test_particles_pfx2 PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_particles_pfx2 PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(test_particles_pfx2 PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_particles_pfx2 PRIVATE JahshakaEngine Qt6::Gui)
add_dependencies(test_particles_pfx2 JahshakaEngineMedia)
add_test(NAME engine.particles_pfx2 COMMAND test_particles_pfx2 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.particles_pfx2 PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 900 LABELS "particles")

# The same suite over the SANITISED copy of the backend (tests/engine defines
# JahshakaEngineAsan; tests/ adds engine before particles). Particle definitions
# outlive their nodes by design and their datablocks are destroyed after the
# SceneManager — exactly the shape of lifecycle that hides read-after-destroy, so it
# gets the same treatment the engine suite gets.
if(JAHSHAKA_ASAN)
    add_executable(test_particles_pfx2_asan test_particles_pfx2.cpp)
    set_target_properties(test_particles_pfx2_asan PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_include_directories(test_particles_pfx2_asan PRIVATE ${CMAKE_SOURCE_DIR})
    target_compile_definitions(test_particles_pfx2_asan PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_compile_options(test_particles_pfx2_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_particles_pfx2_asan PRIVATE -fsanitize=address)
    target_link_libraries(test_particles_pfx2_asan PRIVATE JahshakaEngineAsan Qt6::Gui)
    add_dependencies(test_particles_pfx2_asan JahshakaEngineMedia)
    add_test(NAME engine.particles_pfx2_asan COMMAND test_particles_pfx2_asan
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # Leak detection off for the same reason the engine suite turns it off: the
    # only leaks are libdbus allocations behind the Vulkan loader.
    set_tests_properties(engine.particles_pfx2_asan PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ASAN_OPTIONS=detect_leaks=0"
        TIMEOUT 1200 LABELS "particles")
endif()

# THE FIRE GATE (PARTICLES_FX2_SPEC §11 phase 3). Builds the SHIPPING fire preset
# through the document and the mirror, then asserts the SHAPE of what the engine
# drew: banded luminance, taper, warmth, motion and a clean freeze. Statistical
# by construction — emission is seeded from a process-wide LCG and the affectors
# run on worker threads, so there is no pixel-exact answer to compare against.
# Links IrisGL + JahshakaEngine like the mirror suite (it needs the document).
add_executable(test_particles_fire test_particles_fire.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_particles_fire PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_particles_fire PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_particles_fire PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_particles_fire PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_particles_fire JahshakaEngineMedia)
add_test(NAME particles.fire_shape COMMAND test_particles_fire
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(particles.fire_shape PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "particles")
