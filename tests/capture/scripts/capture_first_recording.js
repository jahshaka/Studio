// capture.first_recording — THE SESSION'S FIRST RECORDING COMPILES NOTHING (VIDEO-REC-2).
//
// VIDEO-REC-1 measured a 585 ms click on the first recording of a session. VIDEO-REC-2 found its
// cause: Qt's encoder probe (the first QMediaFormat::supportedVideoCodecs(Encode) of a process,
// ~0.7 s); the recorder's view and its NV12 job compiled (cold cache) or loaded (warm) on its first
// frames too. The startup shader gate draws that very view (recordingview::create, armed every
// frame) behind the splash, so the first recording's start, warm-up and first recorded frames
// compile and load ZERO shaders. The probe is THIS click's (a project open/create is media-free:
// app.startup_quiet), and it is cheap because the app names Qt Multimedia's ONE hardware backend
// from the engine's adapter (services/mediahardware.h: the unnamed check cost 731 ms).
// FIRST in the pool: the claim is about the process's first recording.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
// A MILLISECOND BAR (TESTING-CLEANUP-2 P10f; tests/support/timingbars.h's rule for a script):
// asserted in capture.first_recording.timing — a copy of this script with JAH_TIMING_BARS defined
// (jah_timing_script), on the quiet box under the GPU-timing lock — and PRINTED here, where the
// pool's arm keeps every count (no shader compiled, none loaded, frames recorded).
function timingBar(cond, msg) {
    if (typeof JAH_TIMING_BARS !== "undefined" && JAH_TIMING_BARS) assert(cond, msg);
    else console.log("time: " + (cond ? "within" : "OVER") + ": " + msg +
                     " (a millisecond bar: capture.first_recording.timing asserts it)");
}

assert(project.create("capture first " + Date.now()) !== false, "project.create");
scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.setCamera({ position: { x: 3, y: 2, z: 4 }, lookAt: { x: 0, y: 0.5, z: 0 } });
// The world's own first frames compile what they compile (not this claim): settled first.
editor.frame(30, 1 / 60);

var sc0 = app.shaderCache();
var st = capture.start({ path: app.dataRoot().root + "/capture-first/first.mp4" });
assert(st !== null, "the first recording of the process starts " + app.lastError());
console.log("the click's own UI-thread time: " + st.startMs + " ms (VIDEO-REC-1's first click: 585 ms)");
var guard = 0;
while (capture.status().frames < 10 && guard++ < 200) editor.frame(1, 1 / 60);
var sc1 = app.shaderCache();
var first = capture.status();
console.log("the first click: " + first.startMs.toFixed(1) + " ms of UI time, Qt's encoder probe " + first.probeMs.toFixed(2) + " ms");
console.log("compiled " + sc0.compiledThisRun + " -> " + sc1.compiledThisRun + ", loaded " + sc0.loadedThisRun +
            " -> " + sc1.loadedThisRun + ", live compiles " + sc0.liveCompiles + " -> " + sc1.liveCompiles);
assert(capture.status().frames >= 10, "frames were recorded: " + capture.status().frames);
assert(sc1.compiledThisRun === sc0.compiledThisRun, "the first recording compiled NO shader: " +
       (sc1.compiledThisRun - sc0.compiledThisRun));
assert(sc1.liveCompiles === sc0.liveCompiles, "and no live compile was reported");
// ...NOR LOADED ONE: the pool keeps its shader cache (--warm), where a recording view the gate
// never drew would still have to LOAD its programs (the NV12 job) on its first frames.
assert(sc1.loadedThisRun === sc0.loadedThisRun, "nor loaded one from the cache: " +
       (sc1.loadedThisRun - sc0.loadedThisRun));
// THE NAMED BACKEND IS THE ONE THE ENCODER USES: on an NVIDIA adapter the app names cuda, and Qt still
// opens NVENC (a named list that silently fell back to libx264 would pass every bar here).
console.log("encoder " + first.encoder + " (hardware " + first.hardwareEncoder + "), backend named: '" +
            first.hardwareBackend + "'");
assert(typeof first.hardwareBackend === "string" && first.hardwareBackend.length > 0,
       "the app named Qt Multimedia's hardware backend from the adapter: " + first.hardwareBackend);
if (first.hardwareBackend === "cuda")
    assert(first.encoder === "h264_nvenc" && first.hardwareEncoder === true,
           "on the NVIDIA adapter the encoder is NVENC: " + first.encoder);
// THE ENCODER PROBE (the real 585 ms): made by THIS first click (never at boot, never by a project
// open/create), and cheap because the backend is named — the first click costs well under 50 ms.
// THE PROBE BAR IS 4 ms (DBUILD-FIX-1; the lead's decision from the lane's table, 2026-10-11). The
// encoder backend's libraries are resident before the click (videorecorder.cpp preloadEncoderBackend:
// the FFmpeg plugin dlopen-ed once per process on a pool thread), so the mechanism left in the probe is
// the backend's own construction: lane max 1.01 ms under 17 compiles (n=20 in the slot, median 0.66).
// x the worst stretch on record (3.5: the end gate's 69.68 ms / the base's 20 ms quiet median) = 3.5 ms,
// rounded up to the next whole ms = 4. A lost preload (>= 20 ms, the dlopen back on the click) fails it
// five times over. The 50 ms click bar below is unchanged.
timingBar(first.probeMs < 4, "Qt's encoder probe on the first click: " + first.probeMs.toFixed(2) + " ms");
timingBar(first.startMs < 50, "the first record click after an open costs under 50 ms of UI time: " +
          first.startMs.toFixed(1) + " ms");
assert(capture.stop({ wait: true, timeoutMs: 30000 }) !== null && capture.status().state === "done", "finished");
console.log("capture.first_recording: PASS");
