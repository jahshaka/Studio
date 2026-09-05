// WARM-UP EFFICACY, as a number (SHADER_CACHE_AUDIT.md F1c).
//
// The question the startup warm-up exists to answer is "how much of a session's
// shader work happens BEFORE the window, behind the splash, rather than on the
// first frames the user sees". This script makes that a measurement instead of
// an argument.
//
// Run it through scripts/shadercache/shadercache-bench.sh --scenario script:...
// so HOME and XDG_CACHE_HOME are both pinned into a scratch tree — otherwise
// the driver's own cache and the developer's real cache are in the numbers.
//
// THE WORLD: whichever project the scratch library already holds. Import it
// once with an unmeasured priming run (`--mode cold` only wipes the shadercache
// directory, so the library DB survives between runs):
//
//   Jahshaka --script <(echo "project.importArchive('scenes/Showroom.zip'); app.quit()")
//
// With an empty library the script measures the startup gate alone, which is
// still the F1 number.
//
// It prints two marker lines the harness greps:
//   EFFICACY-GATE  {...}   what the startup gate had built when the script began
//   EFFICACY-TOTAL {...}   what the whole session had built when it ended
// The interesting ratio is gate.built / total.built: everything not in it is a
// compile the user could have seen.

function snapshot(tag) {
    var s = app.shaderCache();
    var w = app.warmUpSet();
    console.log(tag + " " + JSON.stringify({
        compiled: s.compiledThisRun,
        served: s.loadedThisRun,
        built: s.compiledThisRun + s.loadedThisRun,
        expected: s.expectedShaders,
        pipeline: s.pipelineCacheReason,
        microcode: s.microcodeLoaded,
        hlms: s.hlmsCachesLoaded,
        setBytes: w.sizeBytes,
        shapeSamples: w.shape.samples,
        shapeShadows: w.shape.shadows
    }));
}

// The gate has already run by the time a --script session reaches here: the
// splash hold is inside main(), before the script engine exists. So this IS
// "what was built before the window".
snapshot("EFFICACY-GATE");

var known = project.list();
if (known.length > 0) {
    if (!project.open(known[0].guid)) throw new Error("project.open failed");
    snapshot("EFFICACY-OPEN");
    project.close();
    snapshot("EFFICACY-CLOSE");
}

snapshot("EFFICACY-TOTAL");
app.quit();
