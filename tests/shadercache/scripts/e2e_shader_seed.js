// THE SEED'S BOOT (test_shader_seed_boot.cpp): what the startup gate did, read the moment the script starts — the
// cache's counters and the seed's state, and the heartbeat's longest UI gap since MainWindow started it (the gate's
// longest stall: nothing else ran on the UI thread before this line). Then quit: the clean-quit save writes the
// launch's own cache, which the twin's row turns into the seed.
var s = app.shaderCache();
var h = app.heartbeatStats();
console.log("SHADERSEED " + JSON.stringify({
    enabled: s.enabled, dir: s.dir,
    compiledThisRun: s.compiledThisRun, loadedThisRun: s.loadedThisRun,
    microcodeLoaded: s.microcodeLoaded, microcodeEntries: s.microcodeEntries,
    seedDir: s.seedDir, seedState: s.seedState, seedEntries: s.seedEntries,
    heartbeatRunning: h.running, maxGapMs: h.maxGapMs
}));
app.quit();
