// Cold launch: the verbs report a real cache, saving writes it, clearing
// removes it, and the session carries on with its cache deleted underneath it.
// The driver (test_shader_cache_app.cpp) asserts on the object printed last.

var s = app.shaderCache();
if (!s.enabled) throw new Error("shader cache disabled in a default session");

// Save on demand, then read the file count back through the same verb.
if (!app.saveShaderCache()) throw new Error("app.saveShaderCache() failed");
s.afterSaveFiles = app.shaderCache().files;

// Clear, and prove the running session is unharmed: the verbs still answer and
// the app still quits cleanly (the shaders it needs are already in memory).
if (!app.clearShaderCache()) throw new Error("app.clearShaderCache() failed");
s.afterClearFiles = app.shaderCache().files;

console.log("SHADERCACHE " + JSON.stringify(s));
app.quit();
