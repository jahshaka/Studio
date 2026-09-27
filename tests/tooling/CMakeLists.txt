# devprocess.gpu_lock — THE GPU-TIMING LOCK HOLDS (lane DEVPROCESS-1, docs/TESTING_GATE.md §4).
# scripts/gpu-exclusive.sh is what makes the timing suites exclusive ACROSS ctest processes
# (RUN_SERIAL only reaches inside one); this row proves the wrapper excludes, bounds its wait,
# passes the exit code through and releases on a kill. It runs on a PRIVATE lock file in its
# own binary dir, so it never waits on — or blocks — a real timing suite. No display, no GPU.
add_test(NAME devprocess.gpu_lock
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/gpu_lock.sh"
                 "${JAH_GPU_EXCLUSIVE}" "${CMAKE_CURRENT_BINARY_DIR}/gpu-lock-run")
set_tests_properties(devprocess.gpu_lock PROPERTIES TIMEOUT 60 LABELS "tooling")
jah_no_display(devprocess.gpu_lock)

# devprocess.vram_admit — THE VRAM BUDGET HOLDS (lane GATE-ADMIT-1, docs/TESTING_GATE.md §4b).
# scripts/gpu-admit.sh bounds what every lane's gate puts on the card at once (12 box-wide flock
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
