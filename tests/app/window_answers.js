// app.window_answers (COLD-BOOT-1 stage 4, the lead's added acceptance (a)) - THE WINDOW ANSWERS THROUGHOUT THE STARTUP GATE.
// The boot before this script IS the subject: a virgin home's gate compiling the startup set cold (--clear-shader-cache).
// The UI thread's heartbeat (services/mainthreadheartbeat.h: a 250-ms timer on the UI thread, started by the watchdog
// BEFORE the gate and never reset before this line) records the WORST gap between its own ticks - a UI thread inside a
// long call cannot tick, so the gap IS the freeze. Read FIRST, before anything of this script runs on the UI thread.
// THE LAW, IN THE HEARTBEAT's OWN TICKS: the gate never stood the UI thread still for two ticks (maxGapMs < 2 x
// intervalMs): the splash's events are pumped after every gate frame and no gate frame waits for a compile. RED on the
// gate that compiled in its frames: one freeze of 2,187 ms with the driver's cache warm (spikes/cold-boot-1/S1b arm (c)),
// 104,178 ms with it cold (arm (a)) - this row's driver cache is its own, primed by the warm-up's cold-on boot.
var hb = app.heartbeatStats();
console.log("heartbeat: " + JSON.stringify(hb));
var ok = hb.running === true && hb.ticks > 0 && hb.maxGapMs < 2 * hb.intervalMs;
console.log((hb.running === true ? "ok: " : "FAIL: ") + "the UI thread's heartbeat ran through the gate (" + hb.ticks + " ticks of " +
            hb.intervalMs + " ms)");
console.log((hb.maxGapMs < 2 * hb.intervalMs ? "ok: " : "FAIL: ") + "the longest UI-thread stall " + Math.round(hb.maxGapMs) +
            " ms (" + (hb.maxGapMs / hb.intervalMs).toFixed(1) + " ticks), under 2 ticks (" + 2 * hb.intervalMs + " ms)");
if (!ok) throw new Error("app.window_answers: the window did not answer throughout the gate");
console.log("app.window_answers: PASS");
