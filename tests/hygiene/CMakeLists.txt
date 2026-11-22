# Source-hygiene gates: assertions about the SOURCE TREE, not about a running
# binary. They cost nothing, need no display and no build artifacts, and they
# exist because a large deletion without a gate grows back one call site at a
# time (the materials-evaluator phase-5 precedent).
add_test(NAME source.no_glsl_path
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_glsl_path.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.no_glsl_path)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.no_glsl_path PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_glsl_path)

# source.fork_stamp — D6-FORK-TOOLING: a configure against an engine install built
# from another fork commit than the ogre-next checkout is REFUSED (the stale-engine
# class: the old C++ linked against the new commit's staged media, silently). The
# script drives cmake/OgreInstallStamp.cmake against scratch fixtures.
add_test(NAME source.fork_stamp
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/fork_stamp.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.fork_stamp)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.fork_stamp PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.fork_stamp)

# source.no_raw_transaction — CLOSE-2 (round 2, H1): the editor's gesture batch
# can only stand down for an owner that announces itself with a DbTransaction
# guard; a raw QSqlDatabase::transaction() on the main connection silently gets
# no transaction at all and loses its rollback. The allowed side-connection
# sites are listed in the script.
# PREVIEWS-1 stage 1: and every transaction on it begins IMMEDIATE at one of the two doors (DbTransaction, the
# batch) - a second writing connection (the preview writer) makes a deferred one fail with 517.
add_test(NAME source.no_raw_transaction
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_raw_transaction.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.no_raw_transaction)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.no_raw_transaction PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_raw_transaction)

# source.no_ui_folder_delete — STUDIO-HYGIENE-1 stage 2: no widget deletes a folder tree; the project
# delete is ProjectService::removeProject's (renamed aside, removed on a worker). See the script's header.
add_test(NAME source.no_ui_folder_delete
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_ui_folder_delete.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.no_ui_folder_delete PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_ui_folder_delete)

# source.one_asset_name_write — STUDIO-HYGIENE-1 stage 3: an asset's name is written only by assettags::write
# (no renameAsset call; updateAssetMetadata called only there). See the script's header.
add_test(NAME source.one_asset_name_write
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_asset_name_write.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.one_asset_name_write PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_asset_name_write)

# source.one_save_door — STUDIO-HYGIENE-1 stage 1: the scene has one save door (ProjectService::save); the three
# old doors are gone and the scene row's writers are called only there. See the script's header.
add_test(NAME source.one_save_door
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_save_door.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.one_save_door PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_save_door)

# source.one_undo_push — STUDIO-HYGIENE-1 audit: every scene command is pushed through UndoService (the edit gate,
# the run's macro, the push count "unsaved" reads). See the script's header.
add_test(NAME source.one_undo_push
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_undo_push.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.one_undo_push PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_undo_push)

# source.db_pointers_initialised — DBPTR-1: a declared `Database *` is an
# initialised one, and the two handles that are filled by a setter after
# construction stay wired to their caller. See the script's header for the
# class (24 members; two never assigned at all).
add_test(NAME source.db_pointers_initialised
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/db_pointers_initialised.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.db_pointers_initialised)   # THE SHIP SET (TEST-CUT-1)
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
jah_ship_row(source.panel_rows_guarded)   # THE SHIP SET (TEST-CUT-1)
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
jah_ship_row(source.bake_key_guard)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.bake_key_guard PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.bake_key_guard)

# source.no_engine_transform_reads — ENGINE-UPGRADE-U2 stage 1 (DOCSTORE): the document never asks the engine where
# a node is. No `_get…Updated` read outside irisgl/engine/src — the document's world transforms are its own
# transform store's (nodegraph.h). See the script's header.
add_test(NAME source.no_engine_transform_reads
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_engine_transform_reads.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.no_engine_transform_reads)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.no_engine_transform_reads PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_engine_transform_reads)

# source.one_readfile — IMPORT-1: every assimp parse of a model path goes
# through iris::readSceneFile, which is where the asset's baked import
# transform (scale, rotation, origin) is applied. A parse that skips it renders
# a different size from the asset's own bake, silently. See the script's header.
add_test(NAME source.one_readfile
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_readfile.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.one_readfile)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.one_readfile PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_readfile)

# source.assimp_import_only — SHIPPED-BAKES-1: assimp is reached ONLY from the
# import pipeline. Every reader after an import reads a BAKE (a model's mesh
# bake, a clip's clip bake, a shipped mesh's seed row); a parse anywhere else is
# a second geometry path beside the bake. See the script's header for the list.
add_test(NAME source.assimp_import_only
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/assimp_import_only.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.assimp_import_only)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.assimp_import_only PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.assimp_import_only)

# source.one_material_resolve — MATERIAL-PREVIEW-1: there is ONE way to resolve
# a material (SceneEditService::resolveMaterial) and a hydrated material never
# goes into an Asset's QVariant. The live hover preview was dead for two of its
# three sources for weeks because a QVariant payload cannot be type-checked and
# nothing about the mismatch is visible at a call site. See the script's header.
add_test(NAME source.one_material_resolve
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_material_resolve.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.one_material_resolve)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.one_material_resolve PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_material_resolve)

# source.photon_never_pinned — WORLD-MODE-1: the `photon` World row is never a pin (each
# World Mode runs Photon at its own name and re-picking snaps it back); the reader deletes
# a stored one. See the script's header.
add_test(NAME source.photon_never_pinned
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/photon_never_pinned.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.photon_never_pinned)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.photon_never_pinned PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.photon_never_pinned)

# source.material_page_identity — LEGACY-CONVERT-CRUD fix round: the Materials
# page takes on a material in ONE place, after its graph has loaded. Five tile
# handlers used to write the page's saving identity BEFORE the read, so a
# refused graph left the page editing the previous material under the refused
# material's guid with the save armed — the next drag wrote one material's
# graph into another material's row. See the script's header.
add_test(NAME source.material_page_identity
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/material_page_identity.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.material_page_identity)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.material_page_identity PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.material_page_identity)

# source.gate_scope_rules — THE SCOPING TOOL'S OWN GUARD (STUDIO-SMALL-A, the
# gate-scope trap of ledger §620 item 5). gate-scope.py decides which suites a
# lane runs, so its defects are invisible by construction: a wrong answer is a
# SHORTER gate, and a shorter gate passes. `--build .` from inside the build dir
# used to resolve against the repo root, where ctest prints an EMPTY inventory
# and exits 0 — so every suite the rules chose was filtered out and the tool
# reported "NOTHING to gate" with exit 0. See the script's header for the four
# cases (and the one legitimate empty selection, which must stay legitimate).
find_package(Python3 COMPONENTS Interpreter QUIET)
# A MISSING PYTHON IS LOUD, not silent (the fix-round read, item 8): two gates
# in this directory are python, and an `if(FOUND)` that drops them without a
# word is a tree whose gate quietly got smaller. Named, both of them, so the
# line says what is not being run.
if(NOT Python3_Interpreter_FOUND)
    message(WARNING
        "no python3 interpreter found: the source gates source.gate_scope_rules (the scoping "
        "tool's own guard), source.notices_coverage (every vendored component is "
        "acknowledged) and source.member_init (every member of ours carries a value) "
        "are NOT registered in this build, and neither is "
        "document.reader_defaults in its own directory, or source.no_raw_sheets. Install "
        "python3 and reconfigure.")
endif()
if(Python3_Interpreter_FOUND)
    # source.no_raw_sheets — the static half of the no-raw-stylesheet law (CODING_RULES §2), restored by
    # STUDIO-HYGIENE-1 stage 4 from archive/tests/theme with NO allowlist and a third rule: a CSS-shaped string
    # literal outside src/ui/style/ (a sheet passed as a named constant). See the script's header.
    # source.no_live_read_at_write — PREVIEWS-1 stage 1: no QSqlQuery member or returned cursor, and a query read one
    # row at a time is scoped or finish()ed before its connection's next write (a live read's WAL snapshot fails
    # that write with 517 once the preview writer has committed). Its limits are in the script's header.
    add_test(NAME source.no_live_read_at_write
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/no_live_read_at_write.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.no_live_read_at_write PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.no_live_read_at_write)
    add_test(NAME source.no_raw_sheets
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/no_raw_sheets.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.no_raw_sheets PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.no_raw_sheets)
    # source.shader_cache_key — TEST-STREAMLINE-1 1a: the hlms layer keys on the shader cache's own code
    # (JAHSHAKA_SHADER_CACHE_CODE_SOURCES), not every engine source, because engine C++ reaches a cached Hlms source only
    # through property and piece values (the entry's key). A NEW engine file that builds shader source text (a piece, a
    # program from a C++ string, the disk cache's calls) reds until it joins that list or the low-level allow-list. See
    # the script's header; its negative control runs first.
    add_test(NAME source.shader_cache_key
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/shader_cache_key.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.shader_cache_key PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.shader_cache_key)
    # source.bake_key_versioned — ATOM-HOUSE-1 (SPECS/V2_CROSS_REFERENCE.md section 2, the audit's findings 2 and d): the
    # files of the bake key's CONTENT hash (irisgl/CMakeLists.txt's _mesh_bake_hashed_sources, read there and cross-checked
    # against CMake's own id) change only together with kFormatVersion - a state law against the record
    # irisgl/import/meshbake.key, no branch and no base, so it holds in any clone and on the bake stage's own branch.
    # source.bake_key_guard keeps the other half (the watched files' acknowledgement line).
    add_test(NAME source.bake_key_versioned
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/bake_key_versioned.py" "${CMAKE_SOURCE_DIR}"
                     "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.bake_key_versioned PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.bake_key_versioned)
    # source.gpu_table_strides — ATOM-HOUSE-1 (the audit's finding 1): GpuScene::kLevelsPerMesh and kSubmeshesPerMesh reach
    # the Hlms media only as properties (GpuScene::setTableStrideProperties, HlmsAtom); no hand `* 8u` copy of either in
    # irisgl/engine/media. The script names its exact patterns and its residual; its negative control runs first.
    add_test(NAME source.gpu_table_strides
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/gpu_table_strides.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.gpu_table_strides PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.gpu_table_strides)
    # source.startup_pass_closure — TEST-STREAMLINE-1 (the stage-1 audit's finding 5): the startup pass's key hashes its whole
    # dependency closure - every file the compiler reads for shaderbuildgate.cpp, their sources, the named extras.
    add_test(NAME source.startup_pass_closure
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/startup_pass_closure.py" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.startup_pass_closure PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.startup_pass_closure)
    # source.code_rows — TEST-STREAMLINE-1 1b: the `code` label (tests/CMakeLists.txt jah_code_label_pass, derived) is exactly
    # the rows that take no card - `spec.sh test --code` runs them with no lock and no warm-up (code_rows.py says the rule).
    add_test(NAME source.code_rows
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/code_rows.py" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.code_rows PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(source.code_rows)
    add_test(NAME source.gate_scope_rules
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_scope_rules.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    jah_ship_row(source.gate_scope_rules)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(source.gate_scope_rules PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(source.gate_scope_rules)
    # gate.selection — THE SELECTOR'S REPLAY (MODULAR-GATE-1): the recorded lanes' diffs
    # re-selected against this build's graph (gate_selection_cases.json): every REAL red of
    # theirs must be selected, no lane may fall back, and the graph's precision holds. Needs the
    # BUILT tree (ninja's deps log) and the recorded shas; a cold run pays ~1 min of `nm` once
    # (cached in the build dir), a warm one ~15 s.
    add_test(NAME gate.selection
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_selection.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    jah_ship_row(gate.selection)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(gate.selection PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(gate.selection)
    # source.shaders_spirv_valid (URGENT #89, PHOTON-7 10-4; rebuilt on the engine's own bytes, NONDET-1 F3, 2026-11-21) -
    # EVERY MODULE THE ENGINE RAN IS VALID SPIR-V, not only what the driver accepts: every entry of the two warm microcode
    # caches (the app's and the engine rows', at the build's key: vertex, pixel AND compute) and the ray tier's built-in
    # SPIR-V through spirv-val, its negative controls the fault's own pattern in a JRL0 frame and a truncated cache
    # (shaders_spirv_valid.py says why, and what the old GLSL re-compile proved that this does not).
    add_test(NAME source.shaders_spirv_valid
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/shaders_spirv_valid.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" "${JAH_GLSLANG_VALIDATOR}")
    jah_ship_row(source.shaders_spirv_valid)   # THE SHIP SET (the lead, 2026-10-27: its dump is the ship's warm-up's)
    # JAH_BOOTS app-off + engine-off: the two caches are those boots' products, so `spec.sh test` warms them before this row
    # (a classless row asks no boot: in a fresh tree a tooling diff's call ran it before any warm-up and it went red,
    # SHIP-CUT-2 2026-11-03)
    set_tests_properties(source.shaders_spirv_valid PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling" JAH_BOOTS "app-off;engine-off")
    jah_no_display(source.shaders_spirv_valid)
    jah_no_warm_store(source.shaders_spirv_valid)
    # gate.serve_red (URGENT #54) — A RED AT THE SAME INPUT IS SERVED-RED, NEVER RE-RUN: ci_gate_check.py serve's
    # decision on synthetic records (a red result at this tree with equal configuration / declaration / data hashes is
    # served-red; an ancestor's red runs; a PASS serves as before; NOTRUN / NOADMIT are not results). No build, no git.
    add_test(NAME gate.serve_red
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_serve_red.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(gate.serve_red PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(gate.serve_red)
    # gate.reach_cache (URGENT FIX LIST #57) — A CACHED REACH IS THE FRESH ONE: ci_gate_check.py serve's per-ancestor
    # selection (Reach.of) cached on disk in the build dir, keyed by the ancestor, the tip, the build stamp, the ctest
    # inventory and the selector's scripts; on two ancestors of HEAD the disk's answer equals the fresh one, the key moves,
    # and a corrupt file is recomputed. Needs the BUILT tree (the graph) and git.
    add_test(NAME gate.reach_cache
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_reach_cache.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(gate.reach_cache PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(gate.reach_cache)
    # source.testing_rules — THE TESTING RULES A LINT CAN READ (MODULAR-GATE-1, T6; §7b):
    # R1 every measuring row is inside the GPU lock; R2 every stage-close row is priced; R3 no
    # copied `ctest -LE` tier; R4 no RUN_SERIAL without its verdict; R5 every app/lint row has a
    # subject that selects it. Each rule is first fed a violation (a lint that cannot fail
    # guards nothing).
    add_test(NAME source.testing_rules
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/testing_rules.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    jah_ship_row(source.testing_rules)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(source.testing_rules PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(source.testing_rules)
    # source.row_budget — A ROW IS A SECONDS-LONG GUARD (DOCS/PROCESS.md): every kept guard's price (scripts/gate-times.txt)
    # within its class's budget (a fixture or CPU row 10 s, an app row 60 s); a `measure` row runs at the ship only. One
    # rule, no "by design": a guard over its budget is red.
    add_test(NAME source.row_budget
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/row_budget.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    jah_ship_row(source.row_budget)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(source.row_budget PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(source.row_budget)
    # source.timeout_cap — EVERY TEST IS CAPPED AT 60 s (the owner's law, 2026-10-20; URGENT #48): every registered row has
    # a TIMEOUT within 60 s and no run bound past it, and no code line of tests/**/CMakeLists.txt writes one over 60
    # (timeout_cap.py; fed violations first).
    add_test(NAME source.timeout_cap
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/timeout_cap.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.timeout_cap PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(source.timeout_cap)
    # tooling.warm_app_cache / warm_cold_rows / warm_cold_calls / warm_key — scripts/warm-app-cache.sh on a scratch tests
    # dir and toy trees, never the tree's shared cache (warm_app_cache.sh <part>; ONE SCRIPT, SEVEN ROWS split by proof,
    # TEST-STREAMLINE-1: the whole ran 35.8 s quiet against a CPU row's 10 s): boots - warm at the build's key it answers at
    # once and boots nothing, no stamp / a moved key / a replaced or missing cache read cold, the fake boots, the budget and
    # the bounded wait (URGENT #52, #106); short - a predicted nothing-built boot's short hold proved by the engine's count, the
    # boot's own printed wall, the hold's label (split from boots by proof, TE-WARM-C: 13.5 s); cold - the cold-on runner's rows (#119, #155, the seeds); coldcall - a call's own
    # bounds (the held card, the deadline, the logs); keys - the key's terms, no top-up, #126's nonce, the path-free stamps;
    # inherit - a fresh tree takes its parent's warm caches at an equal key (TEST-STREAMLINE-1 1a); store - THE BOX's WARM STORE
    # (URGENT #193's class, 2026-11-21): one warm per key per box - published by a tree warm at its key, taken by a verified copy
    # by another at the same key, a torn one refused, a second tree waiting on the key's lock (no card), unset = as before.
    foreach(_wp IN ITEMS "warm_app_cache:boots" "warm_short:short" "warm_cold_rows:cold" "warm_cold_calls:coldcall" "warm_key:keys" "warm_inherit:inherit"
                         "warm_store:store")
        string(REPLACE ":" ";" _wp "${_wp}")
        list(GET _wp 0 _wrow)
        list(GET _wp 1 _wpart)
        add_test(NAME tooling.${_wrow}
                 COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/warm_app_cache.sh" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" ${_wpart})
        set_tests_properties(tooling.${_wrow} PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
        jah_no_display(tooling.${_wrow})
        jah_no_warm_store(tooling.${_wrow})
    endforeach()
    # tooling.gpu_lock_fifo — THE CARD's LOCK IS FIRST COME, FIRST SERVED (URGENT #113): scripts/gpu-exclusive.sh's ticket
    # queue on a private lock (sleeps, no GPU): the grants in ticket order, a dead ticket skipped, --wait-max's bound kept,
    # a bounded waiter's place kept across its exit 75 for its re-call (gpu_lock_fifo.sh).
    add_test(NAME tooling.gpu_lock_fifo
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/gpu_lock_fifo.sh" "${CMAKE_SOURCE_DIR}/scripts/gpu-exclusive.sh")
    set_tests_properties(tooling.gpu_lock_fifo PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.gpu_lock_fifo)
    # tooling.row_lock_env — A ROW AND A HOLD's COMMAND CARRY NO CALL's IDENTITY OF THE LOCK (CARD-QUEUE-1 M1): this row's own
    # environment and a private-lock hold's command hold no JAH_GPU_* variable but those gpu-exclusive.sh's "A HOLD's COMMAND
    # SEES:" line names - never the call's place (JAH_GPU_PLACE) or its caller (JAH_GPU_CALLER) (row_lock_env.sh).
    add_test(NAME tooling.row_lock_env
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/row_lock_env.sh" "${CMAKE_SOURCE_DIR}/scripts/gpu-exclusive.sh")
    set_tests_properties(tooling.row_lock_env PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.row_lock_env)
    # tooling.gpu_lock_sweep — THE HOLD's PROCESSES DIE WITH THE HOLD (URGENT #144): scripts/gpu-exclusive.sh on a private lock
    # (sleeps, no GPU): a hold's escaped (setsid) child sits in the hold's own scope and is swept - at the command's exit, at its
    # --run-timeout, KILLED past the grace when it ignores TERM, by the mark with no user manager, under a holder's nested call -
    # and is gone before the next waiter's grant; the arguments arrive verbatim; a clean hold silent, the scope under 0.2 s.
    add_test(NAME tooling.gpu_lock_sweep
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/gpu_lock_sweep.sh" "${CMAKE_SOURCE_DIR}/scripts/gpu-exclusive.sh")
    set_tests_properties(tooling.gpu_lock_sweep PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.gpu_lock_sweep)
    # tooling.gpu_lock_holds — EVERY HOLD OF THE CARD IS COUNTED ONCE (TEST-STREAMLINE-1 1d): scripts/gpu-exclusive.sh's hold
    # log (<lock>.holds: a HOLD line at the take, a DONE line at the release, by ticket) on a private lock - two in turn,
    # a nested call and an exit 75 writing none, a SIGKILLed holder's HOLD alone, ten at once (gpu_lock_holds.sh).
    add_test(NAME tooling.gpu_lock_holds
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/gpu_lock_holds.sh" "${CMAKE_SOURCE_DIR}/scripts/gpu-exclusive.sh")
    set_tests_properties(tooling.gpu_lock_holds PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.gpu_lock_holds)
    # tooling.gpu_lock_mark — A ctest UNDER THE CARD's LOCK MARKS ITS TREE (TEST-STREAMLINE-1 block D's collision): scripts/
    # gpu-exclusive.sh writes <gitdir>/jah-check.pid around a ctest whose test dir is in a git tree, keeps an outer live mark,
    # takes a stale one, marks nothing for another command, leaves none at an exit 75 (gpu_lock_mark.sh; a toy tree, a fake ctest).
    add_test(NAME tooling.gpu_lock_mark
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/gpu_lock_mark.sh" "${CMAKE_SOURCE_DIR}/scripts/gpu-exclusive.sh")
    set_tests_properties(tooling.gpu_lock_mark PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.gpu_lock_mark)
    # tooling.evidence_cap — THE EVIDENCE IS WHAT EXPLAINS A RED, AND IT IS CAPPED (TEST-STREAMLINE-1 1f): fresh_home.sh's red
    # home without its shader cache (unless the cache is the row's subject), gate_runlog.py's cap on tests/.evidence (an older
    # home's cache shed, output/ to its newest 40 runs and each row's newest red log) - on a toy build dir (evidence_cap.py).
    add_test(NAME tooling.evidence_cap
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/evidence_cap.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(tooling.evidence_cap PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.evidence_cap)
    # source.debug_hot_loops — THE VENDORED HOT LOOPS ARE OPTIMISED IN DEBUG (TEST-STREAMLINE-1 5c): every compile command
    # of zip's, meshoptimizer's and Bullet's four targets' sources carries -O2 in a Debug build (compile_commands.json;
    # debug_hot_loops.py, fed its violations first), so the rule in irisgl/CMakeLists.txt cannot silently drop.
    add_test(NAME source.debug_hot_loops
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/debug_hot_loops.py" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.debug_hot_loops PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.debug_hot_loops)
    # tooling.width_pair.plan / .judge / .refuse / .bars / .gain / .reds / .log / .budget — THE PAIRED WIDTH RUN's TOOL (TEST-STREAMLINE-1 4b; the run itself is card
    # block C): scripts/width_pair.py on toys - a hand-written ctest tree of shell rows claiming the card's 8 slots, a fake
    # lock, a fake nvidia-smi (width_pair_test.py; no card, no display, no lock). ONE SCRIPT, EIGHT ROWS split by proof under the
    # CPU row's 10 s: plan - the row set and every exclusion's reason, the parts, the display refused, the card held (exit 75);
    # judge - the palindrome order's holds (1,2,3,2,1), W rows at once at width W (the fake card's peak), the fixture setup judged, a clean set adopts the
    # widest; refuse - an out-of-memory line, a row past the row cap, width 1 disagreeing with itself (no width, exit 1);
    # bars (the lead's rulings on the 4-5 audit) - a row past x1.5 its width-1 wall, a row past its class budget, each
    # refusing its width, and a 0.02-s row's x3 under the walls' 0.5-s resolution floor refusing nothing; reds - a row red
    # beside a neighbour, and a red whose FAIL line changes beside one, each refusing its width, a red on equal FAIL lines
    # agreeing, the tree's check mark held for the run (URGENT #153); log - the declared open reds left out by name (THE OPEN
    # REDS table), an unreadable list refused, the OOM line read from the run's own ctest.log when the build's
    # Testing/Temporary is removed under it, another check's live mark refusing the run; gain - a width not 15 % faster than the next narrower one, a VRAM peak past
    # 90 % of memory.total and an unread memory.total, each refusing it; budget (the final CPU audit's finding 1) - a row already
    # past its class budget ALONE named at width 1 and left out of the judgement (never charged to the width), --plan naming
    # the rows priced over their budget / 1.5; perrow (block C's ruling) - the per-row width from a run's records: a row adopts
    # width 2 only on its own bars in every width-2 run, the run's VRAM under 90 %, each other row alone with its reason.
    foreach(_wp plan judge refuse bars gain reds log budget perrow)
        add_test(NAME tooling.width_pair.${_wp}
                 COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/width_pair_test.py" "${CMAKE_SOURCE_DIR}"
                         "${CMAKE_CURRENT_BINARY_DIR}/width-pair-${_wp}" ${_wp})
        set_tests_properties(tooling.width_pair.${_wp} PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
        jah_no_display(tooling.width_pair.${_wp})
    endforeach()
    # tooling.owner_home_guard — A TEST NEVER BOOTS THE APP ON THE LOGIN USER's OWN DATA ROOT (TEST-STREAMLINE-1 block A: a hand
    # fresh_home.sh call with no HOME rewrote the owner's shader cache): fresh_home.sh's two command paths and warm_app_boot.sh
    # refuse it, exit 2 (tests/support/owner_home_guard.sh); a fake app, nothing written outside the row's scratch.
    add_test(NAME tooling.owner_home_guard
             COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/owner_home_guard_toy.sh" "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(tooling.owner_home_guard PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.owner_home_guard)
    # tooling.coverage_map — THE PER-ROW MAP IS EACH ROW's OWN, AND A PROCESS ENDING IN _Exit STILL WRITES IT (TEST-STREAMLINE-1
    # stage 2): a toy library and two toy rows built --coverage with the tree's cmake/coverage/gcov_exit_wrap.c, each its own
    # GCOV_PREFIX, read by scripts/coverage_map.py (coverage_map_toy.py; no card).
    add_test(NAME tooling.coverage_map
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/coverage_map_toy.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(tooling.coverage_map PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.coverage_map)
    # tooling.row_env — A ROW RUN BY HAND CARRIES ITS REGISTRATION (URGENT #147): scripts/row-env.sh on a toy ctest build -
    # the row printed as registered, --run applying its ENVIRONMENT and ENVIRONMENT_MODIFICATION in its directory,
    # --scratch writing nothing under the build (a warm fresh_home row's shared cache copied), fixtures and a GPU row
    # without a DISPLAY refused, a GPU row under the lock (row_env.sh; CPU, a private lock file).
    add_test(NAME tooling.row_env
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/row_env.sh" "${CMAKE_SOURCE_DIR}/scripts/row-env.sh")
    set_tests_properties(tooling.row_env PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.row_env)
    # tooling.one_spelling — A TREE IS CONFIGURED UNDER ITS ONE SPELLING (CODING_RULES §4, URGENT #129, WORKFLOW-STREAMLINE-1
    # 6.5): cmake/OneSpelling.cmake in a toy NONE project under a scratch HOME whose Developer is a symlink - a FIRST configure
    # whose source or build dir is spelled through the link's target refused, the link's spelling configuring, the
    # JAH_ONE_SPELLING_LINK=none override, no trigger without a symlink, the re-configure guard kept (one_spelling.sh; CPU).
    add_test(NAME tooling.one_spelling
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_spelling.sh" "${CMAKE_SOURCE_DIR}/cmake/OneSpelling.cmake")
    set_tests_properties(tooling.one_spelling PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.one_spelling)
    # tooling.build_ogre_spelling — build-ogre.sh BUILDS UNDER THE TREE's ONE SPELLING whichever spelling it was called by
    # (CODING_RULES §4, URGENT #208): irisgl's script in a toy irisgl under a scratch HOME whose Developer is a symlink, a stub
    # cmake recording -S, -B and the install prefix - called through the link, from the target's spelling, by an absolute
    # target path, OGRE_PREFIX in the target's spelling: all under the link; JAH_ONE_SPELLING_LINK=none and a real-directory
    # Developer untouched (build_ogre_spelling.sh; CPU, no build).
    add_test(NAME tooling.build_ogre_spelling
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/build_ogre_spelling.sh" "${CMAKE_SOURCE_DIR}/irisgl/scripts/build-ogre.sh")
    set_tests_properties(tooling.build_ogre_spelling PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.build_ogre_spelling)
    # tooling.law_records — A RECORD KEEPS ITS ROW's LAW NUMBERS (HARNESS-3 (b)): scripts/gate_runlog.py over a fake ctest
    # stream keeps every `LAW <name> <value> <unit> [<op> <bar>]` line (tests/support/lawline.h) in the row's record, a
    # green row's and a red row's, one past the 256-KB output cap too (law_records.py; a scratch HOME and run-log dir).
    add_test(NAME tooling.law_records
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/law_records.py" "${CMAKE_SOURCE_DIR}/scripts/gate_runlog.py")
    set_tests_properties(tooling.law_records PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.law_records)
    # tooling.runlog_facts — WHAT gate_runlog.py REFUSES AND WHAT A RECORD SAYS FOR A PRICE (DEV_ERRORS 49 + 52, URGENT #209): a
    # ctest --test-dir refused by name; two calls together get two run ids (the pid); a row's in-frame shader compiles counted
    # (`inframe`), a declared cold row marked, the load over the row and our CPUs recorded (runlog_facts.py; a fake ctest)
    add_test(NAME tooling.runlog_facts
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/runlog_facts.py" "${CMAKE_SOURCE_DIR}/scripts/gate_runlog.py")
    set_tests_properties(tooling.runlog_facts PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.runlog_facts)
    # tooling.built_from_heals (URGENT #187) — A SOURCE NINJA COULD NOT SEE IS HEALED, NEVER STAMPED AS BUILT: a toy CMake +
    # ninja project with the tree's cmake/BuiltFrom.cmake + cmake/built_from.py - a cp -p restore (an old mtime) prints the
    # loud line, is touched, stamps stale=1, and the next pass rebuilds it (stale=0); a fork file names build-ogre.sh and stays
    # stale until the install's stamp is newer; a clean pass and an ordinary edit stamp stale=0; THE TARGETS IT CLAIMS (build by
    # reach, ENGINE-HEADERS-1 stage 0): a by-reach pass claims its targets, a second at an equal state adds, a commit or an
    # edited source starts the set again, and a record whose row's targets the stamp does not claim is never its tip's (cases
    # 5-10). No GPU, no display.
    add_test(NAME tooling.built_from_heals
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/built_from_heals.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(tooling.built_from_heals PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(tooling.built_from_heals)
    # source.no_renders_tracked — A RUN LEAVES NOTHING TRACKED (item 7): no render, home or Testing/ in git under
    # tests/ scripts/ testing/ but the fixture inputs.
    add_test(NAME source.no_renders_tracked
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/no_renders_tracked.py" "${CMAKE_SOURCE_DIR}")
    jah_ship_row(source.no_renders_tracked)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(source.no_renders_tracked PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
    jah_no_display(source.no_renders_tracked)
endif()

# source.member_init — UNINIT-SWEEP-1: every scalar, pointer and enum member of ours
# carries a value, either as a default member initialiser or from every constructor of
# its class. The family this holds at zero was found one symptom at a time —
# ProjectManager::openInPlayMode (every sample-browser open landed in the Player),
# MainWindow::previousSpace (Ctrl+Tab's first press), WorldSettingsWidget::autoSave
# (whether Save Scene appeared was undefined at every launch), CameraNode::isPerspective
# (previews rendered orthographic for years), Mesh::numFaces, the 24 Database pointers of
# DBPTR-1 — and a symptom is a slow way to find a member with no value. The allow file
# holds the nine members whose whole point is to stay unwritten (the maths types'
# Qt::Uninitialized overloads) and is the ONLY thing that belongs there — a red is
# answered with the one-line initialiser. The staged baseline file is gone: the sweep
# finished, so this gate reads zero and stays there. See the script's header for the
# rule and its blind spots.
if(Python3_Interpreter_FOUND)
    add_test(NAME source.member_init
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/member_init.py"
                     "${CMAKE_SOURCE_DIR}")
    jah_ship_row(source.member_init)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(source.member_init PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.member_init)
endif()

# source.ray_views_cached — GATHER-HANG-1 stage 1: every view the ray tier takes of an Ogre texture comes out of its
# one cache (irisgl/engine/src/RayQueryViewCache.h; engine.ray_view_cache proves the cache's law), never a per-frame
# create-and-retire (fifteen sites before it). No code line of any file under irisgl/engine/src (recursive) makes an
# image view outside the cache's make and our own images' two creates, each at its exact count (six in all). Red on
# the base (24 site lines). See the script's header.
if(Python3_Interpreter_FOUND)
    add_test(NAME source.ray_views_cached
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/ray_views_cached.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.ray_views_cached PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.ray_views_cached)
endif()

# source.card_report_layout — GATHER-HANG-1 stage 2: JahCardIndirect_cs.glsl's report is sized to what the shader
# writes. Its layout's one home is irisgl/engine/src/CardReportLayout.h: the shader's highest caReport[k + caMode.z]
# equals kCardReportFrameWords, and the cards' and the irradiance field's report buffers (and the cards' read-back)
# go through cardReportWords. Red on the base: the field's 1 + probes stored two words past its buffer every clear.
if(Python3_Interpreter_FOUND)
    add_test(NAME source.card_report_layout
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/card_report_layout.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.card_report_layout PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.card_report_layout)
endif()

# source.ray_loops_bounded — GATHER-HANG-1 stage 2: every ray query loop is bounded in the shader (CODING_RULES section 2).
# In every shader text of irisgl/engine (comments stripped by webgpu_census.strip), jahRqProceed( appears once outside
# the seam's own define: in jah_rq_alpha.glsl's counted form (JAH_RQ_LOOP_BEGIN_), which terminates its query at
# jahRqStepBoundNow() (never above kJahRqStepBound = 4 x the law, 1024 trips); the host's mirror (ScreenProbeGather.h)
# holds the same numbers. Red on the base: 29 unbounded loops (27 through the three macros, 2 raw). Its negative
# control first. See the script's header.
if(Python3_Interpreter_FOUND)
    add_test(NAME source.ray_loops_bounded
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/ray_loops_bounded.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.ray_loops_bounded PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.ray_loops_bounded)
endif()

# source.view_ray_origin — GATHER-HANG-1 stage 3: every ray that starts on a point the view drew takes the one view-ray
# origin (jah_ray_origin.glsl jahViewRayOrigin: the float bound, the point's error and the drawn surface's bound); every
# other jahRayOrigin( caller is a named hit or cache ray, and a new caller is red until it says which. Red on the base:
# the lamp ray and the glass escape lifted by the float bound alone (the black specks, lights.lamp_ray_origin). Its
# negative control first. See the script's header.
if(Python3_Interpreter_FOUND)
    add_test(NAME source.view_ray_origin
             COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/view_ray_origin.py" "${CMAKE_SOURCE_DIR}")
    set_tests_properties(source.view_ray_origin PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.view_ray_origin)
endif()

# source.notices_coverage — NOTICES-1: every vendored component under
# thirdparty/ and irisgl/thirdparty/ is claimed by app/notices.json, every
# entry's notice file exists and reads like a licence, and no entry points at
# text this repository wrote for the purpose. The next `git submodule add`
# cannot ship unacknowledged: it fails here. See the script's header.
if(Python3_Interpreter_FOUND)
    add_test(NAME source.notices_coverage
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/notices_coverage.py"
                     "${CMAKE_SOURCE_DIR}")
    jah_ship_row(source.notices_coverage)   # THE SHIP SET (TEST-CUT-1)
    set_tests_properties(source.notices_coverage PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.notices_coverage)
endif()

# source.one_fonticons — QTAWESOME-1: one QtAwesome per process (fonticons::
# shared()) and one copy of the vendored sources. The tree carried a second,
# uncompiled copy of thirdparty/qtawesome under the materials module, and a
# fresh 786-entry codepoint map was built and leaked per PROPERTY ROW by a
# member no line ever read. See the script's header.
add_test(NAME source.one_fonticons
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_fonticons.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.one_fonticons)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.one_fonticons PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_fonticons)

# source.settings_keys — STUDIO-CRUD-1 item 10: a settings key has ONE owner
# (src/data/settingkeys.h, or a subsystem's own key function). Fails on a key
# literal read with a default at a second site, and on a literal read or write
# of a key the table declares.
add_test(NAME source.settings_keys
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/settings_keys.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.settings_keys)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.settings_keys PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.settings_keys)

# source.gpu_rows_closure — GATE-ADMIT-1: every row that boots Vulkan is registered on the card's
# slots (jah_gpu_row / jah_gpu_exclusive_test / jah_add_pool: its JAH_CLASS and RESOURCE_GROUPS). Read
# from what ctest will actually RUN (`ctest --show-only=json-v1`): a row whose command or
# environment names an Ogre-linked binary, or runs the pool driver, and is neither registered nor
# jah_no_display() fails, named with its CMakeLists line; --self-test proves the detector names a
# synthetic unregistered row. Unlike its neighbours it reads the BUILD (ldd on the binaries), so
# it runs after the build like every other row of a gate.
add_test(NAME source.gpu_rows_closure
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gpu_rows_closure.py"
                 --build "${CMAKE_BINARY_DIR}" --ctest "${CMAKE_CTEST_COMMAND}"
                 --app "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" --self-test)
jah_ship_row(source.gpu_rows_closure)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.gpu_rows_closure PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
jah_no_display(source.gpu_rows_closure)

# source.row_declares_needs — TEST-NEEDS-1: every row that starts the app declares the World Mode
# it boots and the switchable features it keeps (TIER + NEEDS on jah_gpu_row / jah_add_pool;
# tests/support/test_boot.cmake). Read from what ctest will RUN (the closure's detector): an
# app row without both `set:` entries, with a bad word, or with JAHSHAKA_TEST_TIER/NEEDS typed
# into its ENVIRONMENT or `--test-tier` on its command fails, named with its CMakeLists line;
# --self-test proves a toy row without the words is named.
add_test(NAME source.row_declares_needs
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/row_declares_needs.py"
                 --build "${CMAKE_BINARY_DIR}" --ctest "${CMAKE_CTEST_COMMAND}"
                 --app "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" --self-test)
jah_ship_row(source.row_declares_needs)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.row_declares_needs PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
jah_no_display(source.row_declares_needs)

# source.scene_no_friends — ENGINE-UPGRADE-2 stage 2: OgreScene declares no friend (the ray
# tier's friend access went through the GPU-scene view and PhotonComponent). See the script.
add_test(NAME source.scene_no_friends
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/scene_no_friends.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.scene_no_friends)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.scene_no_friends PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.scene_no_friends)

# source.photon_one_dispatch — ENGINE-UPGRADE-2B stage 3: no raw HlmsCompute dispatch under
# src/photon or in OgreSurfaceCache.cpp; photonDispatch (the GI region's witness) is the one door.
add_test(NAME source.photon_one_dispatch
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/photon_one_dispatch.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.photon_one_dispatch)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.photon_one_dispatch PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.photon_one_dispatch)

# source.graphics_queue_seams — ENGINE-UPGRADE-ASYNC-3 stage 1: irisgl/engine/src names
# mGraphicsQueue only on the allow-listed lines that must be graphics (OpenXR, the blocking
# one-shot submits); every other take of the frame's command buffer is the recording queue's.
add_test(NAME source.graphics_queue_seams
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/graphics_queue_seams.py"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.graphics_queue_seams)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.graphics_queue_seams PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.graphics_queue_seams)

# source.moving_sample — EVERY MOVING SAMPLE SITE READS THE ONE RULE (TEST-STREAMLINE-1 3c, URGENT #157): no raw
# `.frame & 0xFFFF` index left in the engine, the reflection's / gather's / area visibility's sites through
# detail::MovingSequence, the helper restarting on a scene bind or a size change only. See the script.
add_test(NAME source.moving_sample
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/moving_sample.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.moving_sample PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.moving_sample)

# source.scene_maps — A MAP KEYED BY A SCENE's ADDRESS DIES WITH THE SCENE (TEST-STREAMLINE-1 3c audit): every
# scene-pointer-keyed map in the engine is known with its erase, RayQueryTier::forgetScene reaches each before its first
# return, each forget helper erases its maps - so a new scene at a freed address never inherits another's cards or voxel
# sides. See the script.
add_test(NAME source.scene_maps
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scene_maps.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.scene_maps PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.scene_maps)

# source.selftest_shapes — THE SELF-TEST's TWO SHAPES RUN THE SAME FIXTURE (TEST-STREAMLINE-1 3c): the fixture half reads
# neither shape flag, its substitutions are the run's flags', its scripts the file's constants. See the script.
add_test(NAME source.selftest_shapes
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/selftest_shapes.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.selftest_shapes PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.selftest_shapes)

# source.presented_predicate — ONE PREDICATE OF "PRESENTED" (TEST-STREAMLINE-1 3a item 2b): the headless host's editor
# view (OffscreenContract::PresentedStandIn) takes the window's route at every presented-vs-offscreen branch -
# View::presentsAsWindow read at each named site, no isOffscreen() left in the engine but the target's fact, the
# identity born once, only the host's factory declaring it. See the script; the picture proof is card block B (B1).
add_test(NAME source.presented_predicate
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/presented_predicate.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.presented_predicate PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.presented_predicate)

# source.scene_no_photon_members — ENGINE-UPGRADE-2B stage 1: Photon's state lives in
# PhotonComponent, not OgreScene — none of the COMMITTED NAMED LIST (photon_members.txt) is an
# OgreScene member, every one is a PhotonComponent member. See the script.
add_test(NAME source.scene_no_photon_members
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scene_no_photon_members.py"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.scene_no_photon_members)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.scene_no_photon_members PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.scene_no_photon_members)

# source.atom_no_registries — ENGINE-UPGRADE-2 stage 3: Atom's files keep no process-wide mutable
# state (the pass provider aside): the scene's in its AtomComponent, a view's ring in the view, the
# device's objects in HlmsAtom. See the script.
add_test(NAME source.atom_no_registries
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/atom_no_registries.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.atom_no_registries)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.atom_no_registries PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.atom_no_registries)

# source.no_fixed_gi_volume — D4-PHOTON-TIERS: the single scene-fitted voxel volume
# stays deleted end to end, and nothing reads its document keys (giCascades,
# giBoundsExcluded) any more — the READER half of "the deleted keys are ignored".
add_test(NAME source.no_fixed_gi_volume
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_fixed_gi_volume.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.no_fixed_gi_volume)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.no_fixed_gi_volume PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_fixed_gi_volume)

# source.shell_boundaries — D10-SHELL-MODULES (the 2026-09-30 audit, area 5: S3,
# S11, S14): no shell/ include under src/modules or src/scripting (the window is
# reached through ui/ishellview.h), no setCurrentIndex(<digit>) in src/shell (the
# pages are keyed by name), and src/shell/mainwindow.cpp includes no module
# header by name (the shell drives modules through StudioModule's hooks).
add_test(NAME source.shell_boundaries
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/shell_boundaries.sh"
                 "${CMAKE_SOURCE_DIR}")
jah_ship_row(source.shell_boundaries)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(source.shell_boundaries PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.shell_boundaries)

# source.half_store_rounded (REDS-1 item 5): an rgba16f image store TRUNCATES on NVIDIA 595.x (measured: gi.card_truth's
# stored texels all in (-1.25, 0] half-float steps), so every half-float store rounds first - jahRoundHalf
# (jah_half_round.glsl / the piece JahHalfRound). Every imageStore into an rgba16f / rg16f / r16f image of the ray jobs,
# and the Photon light store's and the relight's run-time-format stores, carry it; its negative control first.
add_test(NAME source.half_store_rounded
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/half_store_rounded.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.half_store_rounded PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.half_store_rounded)

# source.rough_lobe_transport (ROUGH-LOBE-HISTORY-1, V2 plan 1.8): THE ROUGH LOBE's HISTORY CARRIED TO THE CURRENT VIEW, in
# rq_probe_integrate.comp's own text - probeLobe and probeLobeBrute one call site each (the loop of two inside roughLobeNow),
# V_prev through jahEyeBefore( eye ), the history read carried by the lobe's change before the blend, gLobeFellBack the V
# evaluation's; its negative control (the bare form) first. The law itself: engine.rough_lobe_transport.
add_test(NAME source.rough_lobe_transport
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/rough_lobe_transport.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.rough_lobe_transport PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.rough_lobe_transport)

# source.compute_size_keys (COLD-BOOT-1 stage 2): NO COMPUTE PERMUTATION IS KEYED ON A SIZE NOBODY READS - every job of
# irisgl's compute JSONs sets "inform_shader_of_texture_size" false unless its source or a piece reads a size property
# (a per-run value in a key: one permutation per view size - the editor, the recorder, a PiP, a headset's eye). The files
# are read where the engine reads them, the staged media. Its negative control first.
add_test(NAME source.compute_size_keys
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/compute_size_keys.py" "${CMAKE_SOURCE_DIR}"
                 "${JAHSHAKA_MEDIA_DIR}")
set_tests_properties(source.compute_size_keys PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.compute_size_keys)

# source.gate_pace (COLD-BOOT-1 stage 4): THE STREAMING PACE IS THE GATE's AND THE VIEWPORT's, NOWHERE ELSE - a Complete
# frame owes the whole picture (the reveal law), the startup gate's frames are shown to nobody: only src/app/
# shaderbuildgate.cpp (gateFrame, every frame of the gate) and the viewport's own pace set Streaming; and the gate's scope guard gives
# the pace (in force until set again) and its kept view's compiles back on every way out. Its negative control first.
add_test(NAME source.gate_pace
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/gate_pace.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.gate_pace PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.gate_pace)

# source.test_arms_unreachable (SHADER-SYSTEM-1): A TEST ARM IS A FIXTURE's DOOR - shaders.asyncFail and
# frames.incompleteHold carry the TEST flag in kArms, the scripting verb engine.arm refuses them, and no other src/ caller
# (jahlab's own door apart) sets an arm or calls the fork's _jahFailRequests, and irisgl's own callers (Stillness.cpp's fixed
# measuring arms) name no TEST arm. Its negative control first.
add_test(NAME source.test_arms_unreachable
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/test_arms_unreachable.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.test_arms_unreachable PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.test_arms_unreachable)

# source.webgpu_census (DOORS-0 stage 0, V2 plan 0.5): WHAT WEBGPU REFUSES AND EVERY ONE-EYE-ONLY GATE ONLY FALLS - one
# table (class, regex, path set, ceiling, the plan item that lowers it) over irisgl/engine, comments stripped: device
# address, push constants, image atomics, read-write rg images, texel buffers, early fragment tests, binding arrays,
# subgroups, 64-bit ints, fp16, nonuniform, geometry/tessellation, draw parameters, scalar layout, multiview (a named VR
# difference), the one-eye-only gates and applyVrViewPolicy's assignments, raw Vulkan per engine file (the ray tier, the
# gather and the VR session exempt). Over a ceiling = RED with every site; under = RED "lower the ceiling to M in the same
# commit". Its self-test first: the stripper, one violation per class, the law both ways.
add_test(NAME source.webgpu_census
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/webgpu_census.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.webgpu_census PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.webgpu_census)
# source.engine_headers (ENGINE-HEADERS-1 stage 1, the spec's call 13): THE ENGINE's PUBLIC HEADERS ARE ONE SUBJECT EACH AND ONLY GET
# SMALLER - Types.h gone (no header, no includer); Engine.h's own engine includes exactly the BASE + Fwd.h; every public header's
# line count and header fan-out (H4: the STRUCTURE) held at a ceiling; the source files whose include closure reaches each of
# six heavy headers (H6: the BUILD's WIDTH per file, a static walk; scripts/lead/fanout.sh measures the build's objects)
# held at a ceiling (engine_headers_ceilings.txt), the laws both ways; and the house spelling of an
# irisgl include in the Studio tree, "irisgl/<dir>/..." (the lead's, a finding of NOQT-CORE-1's final read). Its self-test first:
# one synthetic violation per law on a toy tree. Sources only (no build).
add_test(NAME source.engine_headers
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/engine_headers.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.engine_headers PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.engine_headers)
# source.no_qt_below (NOQT-CORE-1 stage 0, V2 plan 0.12): NO Qt BELOW THE EDITOR's UI, AND WHAT IS LEFT ONLY FALLS - per line
# of no_qt_below_ceilings.txt (the irisgl/* directories: every Qt token and include; one QtSql-only line per src/* directory),
# comments and literals stripped, the law both ways (over = RED with every site; under = RED "lower the ceiling to M in the
# same commit"); the Qt-free IrisCore target read from the build (no Qt include directory on its compile lines, no Qt symbol
# in its archive, no include outside core/ and the standard library); nothing under irisgl/ includes src/app/qtstd.h. Its
# self-test first.
add_test(NAME source.no_qt_below
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/no_qt_below.py" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
set_tests_properties(source.no_qt_below PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_qt_below)
# source.ray_seam (DOORS-0 stage 1, V2 plan 0.5): THE CARD'S RAY API IS SPELLED IN ONE FILE - comments stripped, no
# rayQuery*EXT / gl_RayQuery*EXT / gl_RayFlags*EXT / accelerationStructureEXT outside irisgl's
# engine/src/rayquery/include/jah_rq.glsl (irisgl/engine and the fork's Hlms media read), and the seam holds #defines
# only (no function: no OpFunction, no OpName - the compiled words stay the card's own). RED with every site; its
# self-test first (one call per family red outside the seam and quiet in a comment; a function in the seam red).
add_test(NAME source.ray_seam
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/ray_seam.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.ray_seam PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.ray_seam)
# source.display_guard (URGENT #195, 2026-11-21): NO TOOL BOOTS ON A DISPLAY THAT IS NOT A RIG - an agent's shell inherits
# DISPLAY=:0, the OWNER's, and warm-app-cache.sh booted jahlab there. scripts/display-guard.sh is the one definition (:60-:99);
# the census classes every user of DISPLAY under scripts/ and tests/support/ (a new one is red until classed); warm-app-cache.sh
# refuses :0 (exit 2, a safe line) or with --rig takes the tree's rig, the lock unsets it for its command, fresh_home.sh refuses
# it. CPU only: fakes for the app, jahlab and Xvfb, a private lock (display_guard.sh).
add_test(NAME source.display_guard
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/display_guard.sh" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
set_tests_properties(source.display_guard PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
jah_no_display(source.display_guard)
jah_no_warm_store(source.display_guard)
# source.display_guard_linked + source.display_guard_toy (DEV_ERRORS 37 + 38, 2026-11-22): NO TEST BINARY REACHES A DISPLAY THAT
# IS NOT A RIG - a builder ran a Qt test binary directly with the shell's DISPLAY=:0 / Wayland session, twice in one night.
# tests/support/display_guard.cpp runs before main in EVERY test executable (tests/cmake/display_guard.cmake's walk, no line per
# target). _linked: every binary ctest runs carries it (the app and jahlab are not test targets), and the walk reaches a target
# in a NEW subdirectory (a scratch project). _toy: the guarded probe with DISPLAY=:0, a demanded xcb, and the agent shell's
# Wayland session against recording fakes - offscreen, zero connects, REFUSED where X is demanded; the unguarded probe (the
# negative control, the one target the walk skips) connects. CPU only, no real display.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(display_guard_probe display_guard_probe.cpp)
add_executable(display_guard_probe_unguarded display_guard_probe.cpp)
set_target_properties(display_guard_probe display_guard_probe_unguarded PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_target_properties(display_guard_probe_unguarded PROPERTIES JAH_DISPLAY_GUARD_CONTROL ON)
target_link_libraries(display_guard_probe PRIVATE Qt6::Gui)
target_link_libraries(display_guard_probe_unguarded PRIVATE Qt6::Gui)
add_test(NAME source.display_guard_linked
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/display_guard_linked.py" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
add_test(NAME source.display_guard_toy
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/display_guard_toy.py"
                 $<TARGET_FILE:display_guard_probe> $<TARGET_FILE:display_guard_probe_unguarded>)
set_tests_properties(source.display_guard_linked source.display_guard_toy PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
jah_no_display(source.display_guard_linked source.display_guard_toy)
# source.on_demand_rows + source.on_demand_toy (ENGINE-HEADERS-1 stage 0): A ROW RUNS ONLY BINARIES THE CALL BUILT - rows: every
# row whose binary is out of the default build (the sanitizer twin's `*_asan` executables, jah_on_demand in tests/CMakeLists.txt)
# runs through tests/support/on_demand.sh and names its target in JAH_BUILD_TARGETS, and every CODE row whose command is a
# script declares JAH_BUILD_TARGETS (jah_builds: `spec.sh test --code` builds by reach, scripts/row_targets.py); toy: on_demand.sh
# fails by name on a missing stamp, a missing binary and a stamp that does not claim its target (`all` alone, other targets, a
# name containing its own) or claims it on a STALE pass (stale= / stale_fork= not 0), and runs a claimed one with its arguments
# and its exit; and row_targets.py gives an executable that launches the app (JAHSHAKA_BINARY) the app's target. CPU only
# (on_demand.py).
add_test(NAME source.on_demand_rows
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/on_demand.py" rows "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
add_test(NAME source.on_demand_toy
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/on_demand.py" toy "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.on_demand_rows source.on_demand_toy PROPERTIES TIMEOUT 60 LABELS "hygiene;tooling")
jah_no_display(source.on_demand_rows source.on_demand_toy)
# source.no_box_paths (CODING_RULES § 7, the owner and the lead 2026-11-21: THE STUDIO REPO KNOWS NO BOX): no tracked Studio or
# irisgl file names the lead repo's box-local root, literally or composed; today's two namings sit in the
# shrink-only tests/hygiene/box_paths_pending.txt with their fix (BOX-SILO-1 stage 3, S1/S2). Its self-test first.
add_test(NAME source.no_box_paths
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/no_box_paths.py" "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
set_tests_properties(source.no_box_paths PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_box_paths)

# source.script_neutral (SCRIPT-IN-ENGINE-1 stage 1): THE ENGINE'S SCRIPT HOST IS LANGUAGE-NEUTRAL - the JS engine is named by
# irisgl/engine/src/script/js_*.cpp alone, the value, the registry and the host include no Qt, Ogre or JS header, and
# jahengine's NEEDED list holds no Qt (script_neutral.py says the rules). Its self-test first.
add_test(NAME source.script_neutral
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/script_neutral.py" "${CMAKE_SOURCE_DIR}"
                 "${CMAKE_BINARY_DIR}")
set_tests_properties(source.script_neutral PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.script_neutral)

# source.script_build_id (SCRIPT-IN-ENGINE-1 2a a3): THE SCRIPT HOST IS OUTSIDE THE ENGINE BUILD ID, AND IT IS A HOST - the
# build's own EngineBuildId.cmake inputs (build.ninja) name no file of irisgl/engine/src/script/ or jahengine/; those files
# include only the public engine API, script/, the standard library and (js_*.cpp) the JS engine; and the ID, recomputed
# by the build's own script on a scratch copy, is the build's, stays put when a command file is edited and moves when an
# engine input is (script_build_id.py says the rules). Its self-test first.
add_test(NAME source.script_build_id
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/script_build_id.py" "${CMAKE_SOURCE_DIR}"
                 "${CMAKE_BINARY_DIR}" "${CMAKE_COMMAND}")
set_tests_properties(source.script_build_id PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.script_build_id)

# source.brick_list_bounded (NONDET-1 fix items D; READ-8 R8-1..R8-3): THE FEEDBACK'S DATA-DRIVEN READS ARE BOUNDED IN THE
# SHADER - every read of the GPU-written brick list by the group index (the bounce, the copy-back, the directional steps
# 0 and 1, the ray tier's sides gather) returns at or past the list's length and at a brick (or texel) past the volume;
# the side-gather scratch is bounded by its length; the feedback's box loops read min( fbFlags.y, 16u ), ACCUM's counts
# are clamped to the source's bricks and its bricks to the source's dims. Its negative control first.
add_test(NAME source.brick_list_bounded
         COMMAND ${Python3_EXECUTABLE} -I "${CMAKE_CURRENT_SOURCE_DIR}/brick_list_bounded.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.brick_list_bounded PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.brick_list_bounded)

# source.scene_time_one_meaning — A SCENE's TIME HAS ONE MEANING AND ONE SOURCE (ONE-FRAME-1 stage 1): no reader of Ogre's
# process-wide frame clock in our C++ (its one writer OgreEngine.cpp), no media on Ogre's time auto-params (the meter's
# and the OldMovie look's time are set per view from the view's scene), the meter's time its scene's frame time, no
# host-written delta left anywhere, the harness's clock door no src/ caller. Fed a violation first. See the script.
add_test(NAME source.scene_time_one_meaning
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scene_time_one_meaning.py" "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.scene_time_one_meaning PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.scene_time_one_meaning)

# WHAT EACH SCRIPT ROW HERE NEEDS BUILT (jah_builds, tests/CMakeLists.txt; build by reach): every one read for what its script
# runs or reads of the build (ENGINE-HEADERS-1 stage 0, 2026-11-22).
# nothing the build makes - the sources, its own toys, ctest's listing, compile_commands.json, configure outputs:
jah_builds(none ROWS source.no_glsl_path source.fork_stamp source.no_raw_transaction source.no_ui_folder_delete
    source.one_asset_name_write source.one_save_door source.one_undo_push source.db_pointers_initialised
    source.panel_rows_guarded source.bake_key_guard source.no_engine_transform_reads source.one_readfile
    source.assimp_import_only source.one_material_resolve source.photon_never_pinned source.material_page_identity
    source.no_live_read_at_write source.no_raw_sheets source.shader_cache_key source.bake_key_versioned
    source.gpu_table_strides source.code_rows gate.serve_red source.testing_rules source.row_budget source.timeout_cap
    tooling.gpu_lock_fifo tooling.row_lock_env tooling.gpu_lock_sweep tooling.gpu_lock_holds tooling.gpu_lock_mark
    tooling.evidence_cap source.debug_hot_loops tooling.width_pair.plan tooling.width_pair.judge tooling.width_pair.refuse
    tooling.width_pair.bars tooling.width_pair.gain tooling.width_pair.reds tooling.width_pair.log tooling.width_pair.budget
    tooling.width_pair.perrow tooling.owner_home_guard tooling.coverage_map tooling.row_env tooling.one_spelling
    tooling.build_ogre_spelling tooling.law_records tooling.runlog_facts tooling.built_from_heals source.no_renders_tracked
    source.member_init source.notices_coverage source.one_fonticons source.settings_keys source.gpu_rows_closure
    source.row_declares_needs source.scene_no_friends source.photon_one_dispatch source.graphics_queue_seams
    source.moving_sample source.scene_maps source.selftest_shapes source.presented_predicate source.scene_no_photon_members
    source.atom_no_registries source.no_fixed_gi_volume source.shell_boundaries source.half_store_rounded
    source.rough_lobe_transport source.gate_pace source.test_arms_unreachable source.webgpu_census source.ray_seam
    source.display_guard_toy source.on_demand_rows source.on_demand_toy source.no_box_paths source.brick_list_bounded
    source.scene_time_one_meaning source.ray_views_cached source.card_report_layout source.ray_loops_bounded
    source.view_ray_origin source.engine_headers)
# the warm-up's key of this build (scripts/warm-app-cache.sh --key: the staged media and the engine's build id):
jah_builds(JahshakaEngineMedia JahshakaEngineBuildId ROWS tooling.warm_app_cache tooling.warm_short tooling.warm_cold_rows
    tooling.warm_cold_calls tooling.warm_key tooling.warm_inherit tooling.warm_store source.display_guard)
jah_builds(JahshakaEngineMedia ROWS source.compute_size_keys)                  # <build>/bin/media, the staged media
jah_builds(JahStartupPassId ROWS source.startup_pass_closure)                  # <build>/startup-pass-files.txt
jah_builds(JahshakaEngine JahshakaScript Jahshaka IrisGL jahengine ROWS source.script_neutral)   # nm on each
jah_builds(IrisCore ROWS source.no_qt_below)                                   # nm on IrisCore's archive (+ build.ninja)
jah_builds(JahshakaEngineBuildId ROWS source.script_build_id)                  # the build's generated build-id header (its B3)
# the whole default build: every test binary ctest runs (readelf), the build's whole dependency graph (its deps log)
jah_builds(all ROWS source.display_guard_linked gate.selection gate.reach_cache source.gate_scope_rules)
