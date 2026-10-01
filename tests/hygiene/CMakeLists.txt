# Source-hygiene gates: assertions about the SOURCE TREE, not about a running
# binary. They cost nothing, need no display and no build artifacts, and they
# exist because a large deletion without a gate grows back one call site at a
# time (the materials-evaluator phase-5 precedent).
add_test(NAME source.no_glsl_path
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_glsl_path.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.no_glsl_path PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.no_glsl_path)

# source.fork_stamp — D6-FORK-TOOLING: a configure against an engine install built
# from another fork commit than the ogre-next checkout is REFUSED (the stale-engine
# class: the old C++ linked against the new commit's staged media, silently). The
# script drives cmake/OgreInstallStamp.cmake against scratch fixtures.
add_test(NAME source.fork_stamp
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/fork_stamp.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.fork_stamp PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.fork_stamp)

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

# source.one_material_resolve — MATERIAL-PREVIEW-1: there is ONE way to resolve
# a material (SceneEditService::resolveMaterial) and a hydrated material never
# goes into an Asset's QVariant. The live hover preview was dead for two of its
# three sources for weeks because a QVariant payload cannot be type-checked and
# nothing about the mismatch is visible at a call site. See the script's header.
add_test(NAME source.one_material_resolve
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/one_material_resolve.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.one_material_resolve PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_material_resolve)

# source.material_page_identity — LEGACY-CONVERT-CRUD fix round: the Materials
# page takes on a material in ONE place, after its graph has loaded. Five tile
# handlers used to write the page's saving identity BEFORE the read, so a
# refused graph left the page editing the previous material under the refused
# material's guid with the save armed — the next drag wrote one material's
# graph into another material's row. See the script's header.
add_test(NAME source.material_page_identity
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/material_page_identity.sh"
                 "${CMAKE_SOURCE_DIR}")
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
        "document.reader_defaults or theme.no_raw_sheets in their own directories. Install "
        "python3 and reconfigure.")
endif()
if(Python3_Interpreter_FOUND)
    add_test(NAME source.gate_scope_rules
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_scope_rules.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.gate_scope_rules PROPERTIES TIMEOUT 120 LABELS "hygiene")
    jah_no_display(source.gate_scope_rules)
    # gate.selection — THE SELECTOR'S REPLAY (MODULAR-GATE-1): the recorded lanes' diffs
    # re-selected against this build's graph (gate_selection_cases.json): every REAL red of
    # theirs must be selected, no lane may fall back, and the graph's precision holds. Needs the
    # BUILT tree (ninja's deps log) and the recorded shas; a cold run pays ~1 min of `nm` once
    # (cached in the build dir), a warm one ~15 s.
    add_test(NAME gate.selection
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gate_selection.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(gate.selection PROPERTIES TIMEOUT 300 LABELS "hygiene")
    jah_no_display(gate.selection)
    # source.testing_rules — THE TESTING RULES A LINT CAN READ (MODULAR-GATE-1, T6; §7b):
    # R1 every measuring row is inside the GPU lock; R2 every nightly row is priced; R3 no
    # copied `ctest -LE` tier; R4 no RUN_SERIAL without its verdict; R5 every app/lint row has a
    # subject that selects it. Each rule is first fed a violation (a lint that cannot fail
    # guards nothing).
    add_test(NAME source.testing_rules
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/testing_rules.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(source.testing_rules PROPERTIES TIMEOUT 120 LABELS "hygiene")
    jah_no_display(source.testing_rules)
    # gate.ci_check — THE MERGE REFUSAL (MODULAR-GATE-1 T6; the flake law since TEST-SELECTOR-1):
    # scripts/ci-gate-check.sh <range> refuses unless every row the range selects was run at the tip
    # and its reds are answered — 3/3 solo for a contention-class suite, a recorded verdict for any
    # other; a fork pin bump needs the whole tier (a recorded range against a private run log and a
    # private contention list; ~15 s).
    add_test(NAME gate.ci_check
             COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/ci_gate_check_test.py"
                     "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
    set_tests_properties(gate.ci_check PROPERTIES TIMEOUT 180 LABELS "hygiene")
    jah_no_display(gate.ci_check)
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
    set_tests_properties(source.member_init PROPERTIES TIMEOUT 60 LABELS "hygiene")
    jah_no_display(source.member_init)
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
set_tests_properties(source.one_fonticons PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.one_fonticons)

# source.settings_keys — STUDIO-CRUD-1 item 10: a settings key has ONE owner
# (src/data/settingkeys.h, or a subsystem's own key function). Fails on a key
# literal read with a default at a second site, and on a literal read or write
# of a key the table declares.
add_test(NAME source.settings_keys
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/settings_keys.sh"
                 "${CMAKE_SOURCE_DIR}")
set_tests_properties(source.settings_keys PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.settings_keys)

# source.gpu_rows_closure — GATE-ADMIT-1: every row that boots Vulkan takes the box-wide VRAM
# budget (jah_gpu_row / jah_gpu_exclusive_test / jah_add_pool; docs/TESTING_GATE.md §4b). Read
# from what ctest will actually RUN (`ctest --show-only=json-v1`): a row whose command or
# environment names an Ogre-linked binary, or runs the pool driver, and is neither registered nor
# jah_no_display() fails, named with its CMakeLists line; --self-test proves the detector names a
# synthetic unregistered row. Unlike its neighbours it reads the BUILD (ldd on the binaries), so
# it runs after the build like every other row of a gate.
add_test(NAME source.gpu_rows_closure
         COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gpu_rows_closure.py"
                 --build "${CMAKE_BINARY_DIR}" --ctest "${CMAKE_CTEST_COMMAND}"
                 --app "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" --self-test)
set_tests_properties(source.gpu_rows_closure PROPERTIES TIMEOUT 120 LABELS "hygiene")
jah_no_display(source.gpu_rows_closure)

# source.no_fixed_gi_volume — D4-PHOTON-TIERS: the single scene-fitted voxel volume
# stays deleted end to end, and nothing reads its document keys (giCascades,
# giBoundsExcluded) any more — the READER half of "the deleted keys are ignored".
add_test(NAME source.no_fixed_gi_volume
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/no_fixed_gi_volume.sh"
                 "${CMAKE_SOURCE_DIR}")
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
set_tests_properties(source.shell_boundaries PROPERTIES TIMEOUT 60 LABELS "hygiene")
jah_no_display(source.shell_boundaries)
