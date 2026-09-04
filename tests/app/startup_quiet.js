// app.startup_quiet's payload. The script itself is irrelevant — the suite
// asserts on what the BOOT printed, not on what this returned. One document
// verb so a broken script host still fails the run rather than passing
// silently.
console.log("startup_quiet: booted, openTimings=" + app.openTimings().length);
