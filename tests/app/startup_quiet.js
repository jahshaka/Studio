// app.startup_quiet's payload. The script itself is irrelevant — the suite
// asserts on what the BOOT printed, not on what this returned. One document
// verb so a broken script host still fails the run rather than passing
// silently.
console.log("startup_quiet: booted, openTimings=" + app.openTimings().length);

// THE EDITOR LAYOUT SNAPSHOT (SMALL-FIXES-1): leaving the editor saves the
// nested window's state (DockState::snapshot), which is where Qt warns about
// any UNNAMED toolbar — a boot alone never takes one. So the payload opens a
// project and leaves the editor once; the suite greps the warning's absence.
if (project.create("startup quiet") === false) throw new Error("project.create failed");
if (app.space("desktop") !== true) throw new Error("app.space('desktop') failed");
console.log("startup_quiet: editor layout snapshot taken");
