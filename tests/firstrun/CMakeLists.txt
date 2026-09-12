# app.first_launch — the first-launch greeting's DRIVEN-SESSION predicate
# (src/app/firstrun.h, owner decision D3). A unit: no window, no engine, no
# display. It links clioptions.cpp because the rows are real command lines.
add_executable(test_first_launch test_first_launch.cpp
    ${CMAKE_SOURCE_DIR}/src/app/cli/clioptions.cpp)
set_target_properties(test_first_launch PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_first_launch PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_first_launch PRIVATE Qt6::Core)
add_test(NAME app.first_launch COMMAND test_first_launch
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(app.first_launch PROPERTIES LABELS "app" TIMEOUT 60)
jah_no_display(app.first_launch)
