# db.layer — the library's SQLite layer (src/data/db/, NOQT-DATABASE-1 stage 1) on a throwaway file of its
# own: typed round trips (NULL vs '' vs x''), binds, the named errors, the transaction guard, two
# connections' lock with its holder named, a crash mid-transaction, the version floor. CPU, seconds.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_db_layer test_db_layer.cpp)
target_link_libraries(test_db_layer PRIVATE jahshaka_db Qt6::Core)
add_test(NAME db.layer COMMAND test_db_layer WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(db.layer PROPERTIES TIMEOUT 60 LABELS "db")
jah_no_display(db.layer)
