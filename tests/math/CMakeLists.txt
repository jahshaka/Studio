# math.parity — the iris:: math types against Qt's, bit for bit.
#
# This suite is the licence for the Qt math-type migration. It links Qt6::Gui
# purely to have QVector3D/QQuaternion/QMatrix4x4 present as the REFERENCE
# implementation; the types under test are header-only, so nothing else is
# linked and the suite runs in well under a second.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_math_parity test_math_parity.cpp)
set_target_properties(test_math_parity PROPERTIES AUTOMOC OFF)
target_include_directories(test_math_parity PRIVATE ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_math_parity PRIVATE Qt6::Gui)
add_test(NAME math.parity COMMAND test_math_parity
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(math.parity PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "document")

# No Vulkan, no app process, no allocation the suite does not own.
jah_lsan_lane(math.parity)
