# math.parity — the iris:: math types against Qt's, bit for bit.
#
# This suite is the licence for the Qt math-type migration. It links Qt6::Gui
# purely to have QVector3D/QQuaternion/QMatrix4x4 present as the REFERENCE
# implementation; the types under test are header-only, so nothing else is
# linked and the suite runs in well under a second.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_math_parity test_math_parity.cpp)
set_target_properties(test_math_parity PROPERTIES AUTOMOC OFF)
target_include_directories(test_math_parity PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/irisgl)   # the house spelling "irisgl/core/..."; irisgl's own headers spell "core/..."
target_link_libraries(test_math_parity PRIVATE Qt6::Gui)
add_test(NAME math.parity COMMAND test_math_parity
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(math.parity)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(math.parity PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "document")

# No Vulkan, no app process, no allocation the suite does not own.
jah_lsan_lane(math.parity)

# core.colour_qt_parity — iris::SrgbColor against QColor, bit for bit (NOQT-DOCUMENT-1 stage 3, ruling 3): the
# document's stored colour must answer exactly what QColor did, or a saved scene's bytes move. Qt6::Gui is the
# REFERENCE only, as for math.parity; the type under test is header-only.
add_executable(test_colour_qt_parity test_colour_qt_parity.cpp)
set_target_properties(test_colour_qt_parity PROPERTIES AUTOMOC OFF)
target_include_directories(test_colour_qt_parity PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_colour_qt_parity PRIVATE Qt6::Gui)
add_test(NAME core.colour_qt_parity COMMAND test_colour_qt_parity WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(core.colour_qt_parity PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "document")
jah_no_display(core.colour_qt_parity)

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(math.parity)
