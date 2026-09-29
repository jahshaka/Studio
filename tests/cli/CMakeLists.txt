# app.cli_options — the command-line parser's contract (ledger 150). A unit:
# no window, no engine, no display. It links clioptions.cpp because the rows
# are real command lines, exactly as tests/firstrun does.
add_executable(test_cli_options test_cli_options.cpp
    ${CMAKE_SOURCE_DIR}/src/app/cli/clioptions.cpp)
set_target_properties(test_cli_options PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_cli_options PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cli_options PRIVATE Qt6::Core)
add_test(NAME app.cli_options COMMAND test_cli_options
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(app.cli_options PROPERTIES LABELS "app" TIMEOUT 60)
jah_no_display(app.cli_options)

# app.help_exits — HELP-FLAG-1: `--help`/`--version` answer and exit 0, an unknown `--flag`
# exits 2 with one line, and none of them opens a window or writes a file (the real binary,
# DISPLAY unset: a process that reached QApplication would abort on the missing display).
add_test(NAME app.help_exits
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_help_exits.py"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(app.help_exits PROPERTIES LABELS "app" TIMEOUT 120)
jah_no_display(app.help_exits)
