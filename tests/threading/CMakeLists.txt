# THE THREADING SUITES (SPECS/THREADING_ADOPTION_SPEC.md).
#
# threading.staging_race (gate G3-c) — the explicit frame loop's scene gate:
# renderOneFrame updates exactly the scenes an enabled View draws, and never the
# document's staging managers, which the import worker writes while it runs. The
# suite's own header carries the mechanism, the two parts, and the honest note
# about what TSan can and cannot see here.
#
# Links IrisGL AND JahshakaEngine, like tests/mirror: the subject spans the
# boundary on purpose (document nodes on one thread, the engine's frame on the
# other). Needs a reachable display — it renders through Vulkan, because the
# gate is about which scenes a REAL frame walks.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_staging_race test_staging_race.cpp)
set_target_properties(test_staging_race PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_staging_race PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_staging_race PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_staging_race PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_staging_race JahshakaEngineMedia)
add_test(NAME threading.staging_race COMMAND test_staging_race
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(threading.staging_race PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "threading")

# The race lane (scripts/sanitize.sh tsan): this suite IS an async contract —
# a worker building document nodes while the render loop runs — so it is a
# member by the membership rule at the top of tests/CMakeLists.txt. Read its
# source header for what the lane does and does not prove: Ogre-Next is a
# linked shared library and is NOT instrumented, so TSan slows and re-orders
# the soak rather than reporting the (Ogre-internal) accesses the pre-fix build
# raced on.
jah_tsan_lane(threading.staging_race)

