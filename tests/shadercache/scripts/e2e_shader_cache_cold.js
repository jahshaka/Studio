// Cold launch: the verbs report a real cache, saving writes it, clearing
// removes it, and the session carries on with its cache deleted underneath it.
// The driver (test_shader_cache_app.cpp) asserts on the object printed last.

var s = app.shaderCache();
if (!s.enabled) throw new Error("shader cache disabled in a default session");

// Save on demand, then read the file count back through the same verb.
if (!app.saveShaderCache()) throw new Error("app.saveShaderCache() failed");
s.afterSaveFiles = app.shaderCache().files;

// The warm-up set, as the verbs see it (SHADER_CACHE_AUDIT F1a/F1b/F12): it is
// recorded automatically now, it reports whether the automatic record/replay is
// armed at all, and it carries the pass shape the startup warm-up will match.
var w = app.warmUpSet();
s.warmUpEnabled = w.enabled;
s.warmUpShapeSamples = w.shape.samples;
s.warmUpShapeShadows = w.shape.shadows;

// Clear, and prove the running session is unharmed: the verbs still answer and
// the app still quits cleanly (the shaders it needs are already in memory).
if (!app.clearShaderCache()) throw new Error("app.clearShaderCache() failed");
s.afterClearFiles = app.shaderCache().files;

console.log("SHADERCACHE " + JSON.stringify(s));
app.quit();
