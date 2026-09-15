# Source-hygiene gates: assertions about the SOURCE TREE, not about a running
# binary. They cost nothing, need no display and no build artifacts, and they
# exist because a large deletion without a gate grows back one call site at a
# time (the materials-evaluator phase-5 precedent).
add_test(NAME source.no_glsl_path
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_glsl_path.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.no_glsl_path PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_glsl_path)

# source.no_raw_transaction — CLOSE-2 (round 2, H1): the editor's gesture batch
# can only stand down for an owner that announces itself with a DbTransaction
# guard; a raw QSqlDatabase::transaction() on the main connection silently gets
# no transaction at all and loses its rollback. The allowed side-connection
# sites are listed in the script.
add_test(NAME source.no_raw_transaction
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_raw_transaction.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.no_raw_transaction PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_raw_transaction)

# source.db_pointers_initialised — DBPTR-1: a declared `Database *` is an
# initialised one, and the two handles that are filled by a setter after
# construction stay wired to their caller. See the script's header for the
# class (24 members; two never assigned at all).
add_test(NAME source.db_pointers_initialised
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/db_pointers_initialised.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.db_pointers_initialised PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.db_pointers_initialised)
