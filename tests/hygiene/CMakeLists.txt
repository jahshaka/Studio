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

# source.panel_rows_guarded — PANEL-LIFETIME-1: a panel that retires rows holds
# no raw pointer to one. The retirement is a deleteLater and the event loop
# turns between the threaded open's install stages, between a script run's
# verbs and wherever a panel defers a rebuild, so the handle (RowPtr,
# src/ui/controls/bladerow.h) is what knows whether a row is still on the
# panel. See the script's header for the use-after-free this closed.
add_test(NAME source.panel_rows_guarded
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/panel_rows_guarded.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.panel_rows_guarded PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.panel_rows_guarded)
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

# source.one_readfile — IMPORT-1: every assimp parse of a model path goes
# through iris::readSceneFile, which is where the asset's baked import
# transform (scale, rotation, origin) is applied. A parse that skips it renders
# a different size from the asset's own bake, silently. See the script's header.
add_test(NAME source.one_readfile
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_readfile.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.one_readfile PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_readfile)

# source.gate_scope_rules — THE SCOPING TOOL'S OWN GUARD (STUDIO-SMALL-A, the
# gate-scope trap of ledger §620 item 5). gate-scope.py decides which suites a
# lane runs, so its defects are invisible by construction: a wrong answer is a
# SHORTER gate, and a shorter gate passes. `--build .` from inside the build dir
# used to resolve against the repo root, where ctest prints an EMPTY inventory
# and exits 0 — so every suite the rules chose was filtered out and the tool
# reported "NOTHING to gate" with exit 0. See the script's header for the four
# cases (and the one legitimate empty selection, which must stay legitimate).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME source.gate_scope_rules
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_scope_rules.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.gate_scope_rules PROPERTIES TIMEOUT 120 LABELS "hygiene")
    jah_no_display(source.gate_scope_rules)
endif()
