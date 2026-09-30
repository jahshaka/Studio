# devprocess.gpu_lock — THE TIMING ADMISSION HOLDS THE WHOLE CARD (lane DEVPROCESS-1; one admission
# since TEST-SELECTOR-1, docs/TESTING_GATE.md §4). scripts/gpu-exclusive.sh takes EVERY VRAM token,
# so a timing row measures with no other GPU row on the card; this row proves the wrapper excludes
# timed and untimed rows both ways, bounds its wait, passes the exit code through and releases on a
# kill. It runs on a PRIVATE token directory in its own binary dir, so it never waits on — or
# blocks — a real row. No display, no GPU.
add_test(NAME devprocess.gpu_lock
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/gpu_lock.sh"
                 "${JAH_GPU_EXCLUSIVE}" "${CMAKE_CURRENT_BINARY_DIR}/gpu-lock-run")
set_tests_properties(devprocess.gpu_lock PROPERTIES TIMEOUT 60 LABELS "tooling")
jah_no_display(devprocess.gpu_lock)

# devprocess.vram_admit — THE VRAM BUDGET HOLDS (lane GATE-ADMIT-1, docs/TESTING_GATE.md §4b).
# scripts/gpu-admit.sh bounds what every lane's gate puts on the card at once (11 box-wide flock
# tokens); this row proves the bound on the kernel's own lock table: 14 fake rows of the three
# classes never hold more than 12, no waiter holds part of its request, the lowest free tokens
# go first, a killed holder frees, JAH_VRAM_TOKENS limits, the wait is bounded (exit 75) and the
# command is exec'd in place. A PRIVATE token directory: it never waits on — or blocks — a real
# row. No display, no GPU.
add_test(NAME devprocess.vram_admit
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/vram_admit.py"
                 "${JAH_GPU_ADMIT}" "${CMAKE_CURRENT_BINARY_DIR}/vram-admit-run")
set_tests_properties(devprocess.vram_admit PROPERTIES TIMEOUT 120 LABELS "tooling")
jah_no_display(devprocess.vram_admit)

# devprocess.kernel_journal — the ONE row that reds when the kernel journal is unreadable
# (GATE-ADMIT-1): the pools' Xid read prints a FINDING instead of redding every pool.
add_test(NAME devprocess.kernel_journal COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/kernel_journal.sh")
set_tests_properties(devprocess.kernel_journal PROPERTIES TIMEOUT 60 LABELS "tooling" SKIP_RETURN_CODE 77)
jah_no_display(devprocess.kernel_journal)
