# Crash reporting at beta — the breakpad plan

**Status:** plan only. No code lands from this document today.
**Scope:** STABILITY_PROGRAM_SPEC Lane 8 (audit §5.3). One page, on purpose.
**Today's baseline:** `src/app/crashhandler.cpp` writes a text backtrace for every fatal signal
and is ALWAYS on. Breakpad is vendored (`thirdparty/breakpad`), wired
(`cmake/IncludeBreakpad.cmake`, `src/app/breakpad.h`) and **compiled out of every build we
run** (`-DDISABLE_BREAKPAD=ON` is the standing rule while debugging). Nothing below is needed
before beta; it is what "beta" means for crash reporting.

## 1. What we want at beta

A crash on a machine we do not own produces **one minidump we can symbolise here**, with **no
dialog, no upload prompt, and no behaviour change for the user** beyond the app dying as it
already would. We do not want telemetry, a crash-reporter executable, or a network client in
the app.

## 2. Configuration: dump and re-raise

- `initializeBreakpad()` keeps `install_handler = true` and `server_fd = -1` (in-process
  dumping), exactly as `breakpad.h` already constructs it.
- **The `DumpCallback` stops spawning `crash_handler`.** Both platform callbacks today
  `QProcess::startDetached("crash_handler")` (`breakpad.h:53-65`, `:33-49`) — that is the
  dialog path, and it is also a `new QProcess` inside a signal handler, which is not
  async-signal-safe. The beta callback does nothing but `return succeeded;`.
- Dumps go to `QStandardPaths::AppDataLocation + "/crashes/"`, not `TempLocation` as today
  (`breakpad.h:69`): a beta tester's `/tmp` is cleared on reboot, and "reboot after a crash" is
  the single most likely next action.
- Retention: keep the newest 10, delete the rest at startup. A minidump is ~1-2 MB.
- The user's route to it stays manual and human: "send us the newest file in
  `<AppData>/Jahshaka/crashes/`". No uploader.

## 3. Handler order — the thing to get right, and it is currently backwards

`main.cpp:111-118` installs our handler first and breakpad second, with the comment "it
re-raises, so breakpad chains behind it when enabled". **That is inverted, and worse than
inverted.** POSIX gives the signal to the LAST handler installed, so breakpad would run first;
and breakpad, on a *successful* dump, calls `InstallDefaultHandler(sig)` rather than restoring
what it replaced (`thirdparty/breakpad/breakpad/src/client/linux/handler/exception_handler.cc:383-386`).
The previous handler is restored **only when breakpad fails**. So with breakpad enabled as
wired today, a normal crash writes a minidump and **no `crash-*.log` at all**.

The fix is one line of ordering, and it is the first work item of this plan:

```
initializeBreakpad();     // installed FIRST, so it sits underneath
installCrashHandler();    // installed LAST, so it runs FIRST and then raise()s into breakpad
```

Our handler already restores `gPrev[sig]` and `raise()`s (`crashhandler.cpp:88-90`), so it is
a correct chaining handler; breakpad is not. Ordering it this way yields **both** artifacts on
every crash: the human-readable text log, then the minidump. Verify with a deliberate
`kill -SEGV` on a `-DDISABLE_BREAKPAD=OFF` build — both files must appear.

## 4. Symbolisation: `minidump_stackwalk` in the release pipeline

A minidump is worthless without symbols from the exact binary that produced it, so this is a
**release-pipeline** step, not a developer step.

1. Build the release binary (RelWithDebInfo — the macOS lane already has this two-lane model,
   `docs/PACKAGING_MACOS.md`).
2. `dump_syms <binary> > <module>.sym`; file it as
   `symbols/<module>/<BUILD-ID-from-the-sym-header>/<module>.sym`. Do this for `Jahshaka` and
   for every `.so` we ship (IrisGL, the Ogre-Next family).
3. Archive `symbols/` **beside the release artifact**, keyed by version. A symbol tree that
   does not match the shipped build is the classic failure mode here.
4. To read a dump: `minidump_stackwalk <dump> symbols/ > stack.txt`.

Both tools build out of the vendored breakpad tree (`src/tools/linux/dump_syms`,
`src/processor/minidump_stackwalk`). **Neither is installed on the dev box today**
(STABILITY_PROGRAM_SPEC §1.8) — building them is part of the same work item, and the release
script gains a self-check that refuses to publish without a symbol tree.

## 5. Per-platform tail

| Platform | State | Beta work |
|---|---|---|
| Linux | `breakpad.h` has the path; `USE_BREAKPAD` off | Ordering fix + callback + symbol step |
| Windows | `breakpad.h` has the path (untested — the port is specced, not built) | Prove it once the port builds; `.pdb`-free symbolisation is exactly why breakpad exists on this platform |
| macOS | **Not wired at all** — `cmake/IncludeBreakpad.cmake:6` is `WIN32 OR (UNIX AND NOT APPLE)`, and `crashhandler.cpp` is the only handler there | Either wire the mac client or accept text backtraces only. Recommend the latter for beta: the mac lane already ships symbols in the bundle's dSYM |

## 6. Cost and sequencing

Half a day for §2 + §3 (callback, path, retention, ordering, one `kill -SEGV` gate). Half a day
for §4 (build the two tools, add the symbol step + the refuse-to-publish check). It is
**gated on there being a release pipeline to hang it off**, which today exists only for macOS.
Do it when beta packaging starts, not before — and note that §3's ordering bug is worth fixing
the day someone first flips `DISABLE_BREAKPAD=OFF`, because until then it is unobservable.
