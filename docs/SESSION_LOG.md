# The session log

Jahshaka writes **one log file per run** — the startup header, every scene open, save and
play session, every warning, and a periodic performance sample — into a `logs/` directory
beside the app's other files. This page is how to read it and how to hunt with it.

Design and rationale: `SPECS/SESSION_LOG_SPEC.md`. The verb reference is generated:
`docs/SCRIPTING.md`, module `log`.

## Where it is

| Build | Directory |
|---|---|
| Development (`QT_DEBUG`) | `<working directory>/logs/` — i.e. `build-linux/bin/logs/` when you run the app the way the build docs say |
| Release | `<application data location>/logs/` |

```
logs/jahshaka-2026.09.06-14.22.31-10412.log        the session log
logs/jahshaka-2026.09.06-14.22.31-10412-ogre.log   Ogre's own log, SAME run
logs/latest.log -> the newest session log          (latest.txt on Windows)
logs/jahshaka.log                                  the legacy iris::Logger file
```

`tail -f build-linux/bin/logs/latest.log` is the one command to watch a running editor.

Files are swept **at startup only**: the 10 newest of ours are kept, anything of ours older
than 5 days is deleted, and `crash-*.log` is never touched — those are forensic and are the
one artifact a user is asked to send. `log.path()` names every artifact from a script or an
agent.

## Reading a line

```
[2026.09.06-14.22.31.418][ 1041]scene: Display: opened 'Modern Room' in 2841 ms
 └ local timestamp, ms   └ rendered frame  └ category  └ level (omitted for `Log`)
```

The frame counter is what turns "these three warnings" into "these three warnings **in the
same frame**". It is 0 before the engine starts.

## Levels and categories

Levels are Unreal's, most severe first: `Fatal, Error, Warning, Display, Log, Verbose,
VeryVerbose`, plus `Off`. `Fatal` does **not** terminate here — we are a content tool, and a
new abort path would be a stability regression, not a logging feature.

`Warning` and above are flushed immediately and mirrored to stderr, so a crashed session
still has them. `Log` and below are file-only and buffered.

Categories in v1: `app scene engine ogre render perf shader assets db script ui mirror
physics media qt legacy`. `log.categories()` lists them with their levels and how many
records each has actually emitted.

Verbosity comes from four layers, in this order — **compiled default → `jahsettings.ini` →
command line → runtime**:

```bash
./Jahshaka --log-level=verbose                     # everything, everywhere
./Jahshaka --log-level=mirror=verbose,ogre=log     # per category, repeatable
./Jahshaka --log-dir=/tmp/mylogs                   # somewhere else (what tests use)
./Jahshaka --no-log                                # nothing on disk
```

```ini
[log]
global=display
mirror=verbose
perfSampleSeconds=60
```

```js
log.level('mirror', 'verbose')            // this session only
log.level('mirror', 'verbose', true)      // ...and write the ini key
```

Two things worth knowing:

* **`global` is not a ceiling.** It is the default for every category nobody has set
  explicitly. Lowering it never silently disables a category someone tuned on purpose —
  which is why the production defaults can keep `perf` at `Log` while everything else drops
  to `Display`.
* **`qDebug()` arrives as `Verbose` under `qt`**, and `qt` sits at `Log`. The tree's
  existing `qDebug` tracing is therefore OFF by default even in a Debug build:
  `--log-level=qt=verbose` turns it on.

## Hunting a progressive fps decay

This is the case the log was built for: *"it opened fast and it was slow twenty minutes
later, and there is no record of what happened in between."* Every number needed to diagnose
that already existed as a verb — `app.frameStats`, `app.renderStats`, `app.engineErrors`.
Nothing wrote any of them down. The perf sampler does, once a minute.

**1. Make sure the sampler is on** (it is, by default — 60 s in a dev build, 300 s in a
release one):

```js
log.perf()          // -> {seconds: 60, running: true, defaultSeconds: 60}
log.perf(30)        // tighten it for this session
log.perf(30, true)  // ...and keep it for the next one
```

**2. Work normally.** Annotate as you go — this is what turns a flat file into a timeline
you can correlate against:

```js
var m = log.mark('opening the heavy world');
// ... do the thing ...
log.since(m, {minLevel: 'warning'})   // what went wrong while I did that
```

**3. Read the series.** Pull the `perf` lines out of the file:

```bash
grep 'perf:' build-linux/bin/logs/latest.log
```

```
[…]perf: fps 58.2 work 9.4ms worst 41.2ms slow 3 | draws 812 tris 1.94M
     | rendered 3600 skipped 12 | engineErrors 0 | rss 1284MB | since-start 21m
```

The **shape of the series** is the diagnosis:

| What moves | What it means |
|---|---|
| `work` rises, `draws`/`tris` flat | our code got slower — the same scene costs more per frame |
| `draws`/`tris` rise | the scene grew (something is being added and not removed) |
| `rss` rises, everything else flat | a leak |
| `slow` climbs in steps, `work` flat | hitches, not a slope — look for what happened at each step |
| `skipped` rises, `rendered` flat | nothing is being drawn (no enabled view), not a slowdown |
| `engineErrors` rises | the renderer is refusing work — read the `engine` records |

Note `fps` measures the render loop's **timer** (and vsync), not the renderer's headroom.
`work` is the honest number; `app.frameStats()`'s doc string explains why at length.

**4. Bracket a suspect operation** instead of waiting for the interval:

```js
var before = log.sample();
// ... the suspect operation ...
var after = log.sample();
```

**5. Compare play sessions.** Every `PLAY STOP` line carries a delta of the frame stats
across its bracket, so two play sessions in one run are directly comparable without anyone
having been watching:

```
=== PLAY STOP === 42311 ms | rendered 2489 (58.8 fps avg) skipped 0
    | workMs 9.41 worst 41.2 (session worst 808.0) | slowFrames +2
```

**6. Compare OPENS.** The `SCENE OPEN` block carries the full per-stage ledger plus the
scene's node/mesh/material counts. Open #1 at minute 0 and open #4 at minute 40 in the same
file is exactly the comparison a decay needs — and it is the reason the log is one file per
RUN rather than one per scene.

## What the log deliberately does not do

* It does not merge Ogre's log. Ogre writes thousands of lines per boot; those stay in the
  `-ogre.log` sibling, and only `LML_CRITICAL` (which includes every Vulkan validation
  error) crosses into `engine`. `--log-level=ogre=log` brings the rest through when you want
  it.
* It does not record a line per scripting verb. A script can call thousands; the record is
  one line per script RUN.
* It does not log on the frame path at default verbosity. Not one call site — the sampler is
  a timer, and the one existing per-frame violation (`PlayBack`'s controller-mismatch line)
  was latched rather than left.
* It does not strip itself in a release build. Production cuts *verbosity*, never the
  artifact: a shipped Jahshaka's users file bugs, and the file has to contain the header, the
  scene blocks and every warning.
