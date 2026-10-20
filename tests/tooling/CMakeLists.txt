# devprocess.kernel_journal — the ONE row that reds when the kernel journal is unreadable
# (GATE-ADMIT-1): the pools' and the card lock's Xid read print a FINDING instead of redding every row.
add_test(NAME devprocess.kernel_journal COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/kernel_journal.sh")
jah_ship_row(devprocess.kernel_journal)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(devprocess.kernel_journal PROPERTIES TIMEOUT 60 LABELS "tooling" SKIP_RETURN_CODE 77)
jah_no_display(devprocess.kernel_journal)

