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
2. [ ] Unreal naming: Duplicate + Paste -> "Cube" -> "Cube2" -> "Cube3", unique among siblings.
3. [ ] Navigation: editor fly moves to arrows + PageUp/PageDown; W/A/S/D/Q/E freed.
       Player keeps BOTH. Docs + Preferences text.
4. [ ] Ctrl+A `edit.selectAll` shortcut yielding to text fields.

## Log
- setup done (worktree, irisgl clone, 24 patches, build-ogre.sh, configure ASan)
- step 1 DONE: irisgl 7bb0b87 (lane-mselextras-irisgl) + Studio commit below.
  Verified: scripting.e2e.multiselect PASS (headless). app.multiselect_outline +
  mirror.* need the Vulkan gate (blocked until GO).
