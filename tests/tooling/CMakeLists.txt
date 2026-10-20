# devprocess.kernel_journal — the ONE row that reds when the kernel journal is unreadable
# (GATE-ADMIT-1): the pools' and the card lock's Xid read print a FINDING instead of redding every row.
add_test(NAME devprocess.kernel_journal COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/kernel_journal.sh")
jah_ship_row(devprocess.kernel_journal)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(devprocess.kernel_journal PROPERTIES TIMEOUT 60 LABELS "tooling" SKIP_RETURN_CODE 77)
jah_no_display(devprocess.kernel_journal)

# devprocess.harness_exit — THE HARNESS SAYS HOW ITS CHILD DIED (TEST-SELECTOR-1 H1, plan 9ax
# HARNESS-EXIT-1): tests/support/mcpharness.h holds the QProcess it talks to, and a transport
# failure prints the child's state / exit code / exit status and its last 40 log lines. Proved on
# a stand-in shell child (killed, exiting 5, alive); no app, no display, no GPU.
add_executable(test_harness_exit test_harness_exit.cpp)
set_target_properties(test_harness_exit PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_harness_exit PRIVATE Qt6::Core Qt6::Network)
# spawn() names the app binary; this suite never calls it (a stand-in child is started by hand)
target_compile_definitions(test_harness_exit PRIVATE JAHSHAKA_BINARY="/bin/false")
add_test(NAME devprocess.harness_exit COMMAND test_harness_exit)
set_tests_properties(devprocess.harness_exit PROPERTIES TIMEOUT 60 LABELS "tooling")
jah_no_display(devprocess.harness_exit)
