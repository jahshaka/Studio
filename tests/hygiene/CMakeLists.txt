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

# source.bake_key_guard — BAKEKEY-1: the mesh-bake key's hand-bumped half.
# The producer hash covers the three files that WRITE a bake; the document-side
# classes it used to hash (25 of 408 irisgl commits touched one, on 10 of 11
# working days, staling every library's bakes for edits that could not move a
# baked byte) ride the hand-bumped kFormatVersion instead. A hand bump that can
# be forgotten needs a gate: see the script's header for the rule, the base
# mechanism (the last commit that touched the version line — no branch, tag or
# recorded sha required) and the `bake-output: unchanged` acknowledgement.
add_test(NAME source.bake_key_guard
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/bake_key_guard.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.bake_key_guard PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.bake_key_guard)
