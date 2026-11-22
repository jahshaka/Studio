// Warm launch: report what the startup build did. The whole assertion set lives
// in the driver — this script only has to observe.
// THE COMPUTE LAYER's LAW (SHADER-SYSTEM-1 stage 3F; COLD-BOOT-1 stage 3): the compute counters at the script's start
// (the startup gate's frames behind it) and after the editor's first 30 live frames.
var s = app.shaderCache();
s.asyncAtStart = app.asyncShaders();
// THE OWNER's LAUNCH (SHADER-SYSTEM-1 7a, the lead's ruling): an interactive session's editor compiles in the
// BACKGROUND (main.cpp's policy); a scripted run is synchronous unless it asks - this one asks, before the editor's first
// frame, so a gate gap shows as what the owner meets: a background compile, a placeholder draw, a Complete frame's join.
app.setAsyncShaders(true);
editor.frame(30, 1 / 60);
s.asyncAfter30 = app.asyncShaders();
// ...then until nothing is pending (SHADER-SYSTEM-1 stage 7a: what the editor's frames built since the gate, read
// against app.asyncShaders().atGate).
for (var i = 0; i < 600 && app.asyncShaders().pending > 0; ++i) editor.frame(1, 1 / 60);
s.asyncAfterFrames = app.asyncShaders();
console.log("SHADERCACHE " + JSON.stringify(s));
app.quit();
