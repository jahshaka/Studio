# app.update_check — the launch-time update check (UPDATE-1): its own transfer
# timeout, no orphaned reply, and a failure that is a no-op rather than a
# dialog. A unit: two loopback QTcpServers, no window, no engine, no display.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(test_update_check test_update_check.cpp
    ${CMAKE_SOURCE_DIR}/src/app/updatechecker.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_update_check PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_update_check PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_update_check PRIVATE Qt6::Core Qt6::Network)
add_test(NAME app.update_check COMMAND test_update_check
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(app.update_check PROPERTIES LABELS "app" TIMEOUT 60)
# app.update_check.timing — its two upper millisecond bars, nightly (lane D6B-GATE-SHAPE;
# audit §5 FIX-TIME; the audit's preferred fake clock would change the product's checker,
# out of this lane's scope): the same binary with JAHSHAKA_TIMING_BARS=1, the GPU-timing lock.
jah_gpu_exclusive_test(NAME app.update_check.timing RUN_TIMEOUT 60 COMMAND test_update_check
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(app.update_check.timing PROPERTIES
    ENVIRONMENT "JAHSHAKA_TIMING_BARS=1" LABELS "app;nightly;quiet-box")
jah_no_display(app.update_check app.update_check.timing)
