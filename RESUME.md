# lane-mselextras — RESUME

Lane: multi-select extras (EDITOR_MULTISELECT_SPEC P4 rows) + the navigation change.
Base: Studio `ogre` @ 2b89d91c, irisgl @ 3521dd1.
Worktree: /home/jahshaka/Developer/jahshaka/.claude/worktrees/lane-mselextras
Branches: Studio `lane-mselextras`, irisgl `lane-mselextras-irisgl` (explicit local clone).
Build: build-linux, ASan on, ninja -j6. Ogre built in-worktree with the 24-patch stack.

## RESTRICTION IN FORCE
No app on a display, no Vulkan ctest, no Xvfb until the lead sends GO.
Headless suites + `--headless` script runs only, EXPLICIT DISPLAY on every invocation.

## Steps
1. [x] Primary outline colour (Blender way) — irisgl Scene + SceneMirror, prefs row,
       editor.outline()/setOutline verbs, overlays() report, app.multiselect_outline extension.
2. [x] Unreal naming: Duplicate + Paste -> "Cube" -> "Cube2" -> "Cube3", unique among siblings.
3. [x] Navigation: editor fly moves to arrows + PageUp/PageDown; W/A/S/D/Q/E freed.
       Player keeps BOTH. Docs + Preferences text.
4. [x] Ctrl+A `edit.selectAll` shortcut yielding to text fields.

## Log
- setup done (worktree, irisgl clone, 24 patches, build-ogre.sh, configure ASan)
- step 1 DONE: irisgl 7bb0b87 (lane-mselextras-irisgl) + Studio commit below.
  Verified: scripting.e2e.multiselect PASS (headless). app.multiselect_outline +
  mirror.* need the Vulkan gate (blocked until GO).
- step 2 DONE: services/nodenaming.h|.cpp; duplicateNode + insertFragment rename.
  Verified: services.selection_set PASS (12 naming cases), scripting.e2e.multiselect_edit PASS
  (Cube -> Cube2 -> Cube3 -> Cube4, paste Cube5, free name under another parent kept).
- step 3 DONE: irisgl c7bbb7d (arrow bindings on Move) + Studio commit below.
  Verified headless: input.actions, input.fly_controls, input.axis_view_lock PASS.
  app.navigation_keys (new rig suite) WRITTEN BUT UNRUN — needs the Vulkan/Xvfb gate.
  scripting.e2e.editor_controls updated (Move now 8 keys) — needs a display, unrun.
- step 4 DONE: SceneEditService::selectAll + editor.selectAll verb + edit.selectAll (Ctrl+A)
  with the text-field yield in MainWindow::selectAllActiveSpace.
  Verified headless: scripting.e2e.multiselect PASS. app.multiselect_keys PART 5 written, UNRUN.

## VULKAN GATE (GO received 2026-09-09) — my own Xvfb :220
Ordered run, all green:
  app.selection_outline        PASS (single selection unchanged)
  app.multiselect_outline      PASS after fixing the PROBE LATTICE (not the code):
                               primary=left 200/0, primary=right 0/192, single 0/0
  app.navigation_keys (NEW)    PASS after fixing `jq -r` in the suite:
                               Up 7.832u, PageUp +7.688u, W 0u + gizmo->translate, E 0u
  app.multiselect_keys PART 5  PASS after making the console focus deterministic
                               (Ctrl+` now focuses its input — a real defect, fixed)
  mirror.document_to_engine / mirror.skinned_outline / cameras.body /
  cameras.e2e.axis_lock / scripting.e2e.editor_controls   PASS
Full ctest pass 1: 296/298. shortcuts.registry = intended move, re-pinned.
                             scenegraph.benchmark = external-load flake, solo-green (365 s).
Full ctest pass 2: see below.

## LAST STEP
Remove RESUME.md in the final commit (lead's instruction).
