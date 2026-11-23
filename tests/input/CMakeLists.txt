# input.actions — the gameplay action layer (AVATAR_LOCOMOTION_SPEC §8.2,
# Stage 1's gate): binding round-trip, conflict report, latch semantics.
#
# Links the Qt-free document library (IrisDocument, NOQT-DOCUMENT-1 stage 6) rather than IrisGL: the input layer is the
# document's, with no importer, no mirror and no engine.
find_package(Qt6 REQUIRED COMPONENTS Gui)

add_executable(test_input_actions test_input_actions.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp)
set_target_properties(test_input_actions PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_input_actions PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_input_actions PRIVATE Qt6::Gui IrisDocument)   # IrisDocument: the input map (and IrisCore's iris::Value with it)
add_test(NAME input.actions COMMAND test_input_actions
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(input.actions)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(input.actions PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "document")
# No display, no engine, no GPU — and enforced, not asserted.
jah_no_display(input.actions)
jah_lsan_lane(input.actions)

