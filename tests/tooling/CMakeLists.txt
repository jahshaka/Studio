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
