// scripting.e2e.introspection — the discovery half of the scripting surface.
//
// Runs headless (--headless): every verb here is a document verb.
//
// Lane 0 (AI_SURFACE_PROGRAM_SPEC §2.0) — the contract gate's second half:
//   app.apiProblems() is empty, which is the first time ApiRegistry::validate()
//   has ever run over the REAL module set. The unit test that used to call it
//   (tests/scripting/test_script_engine.cpp) links only the scripting core, so
//   the "real module set" it validated was its own fake module. A verb
//   advertised with no invokable method behind it, a missing doc string or a
//   duplicate name in any of the shipped modules was invisible until now.
//   (The api.contract ctest is the other half: it proves docs/SCRIPTING.md is
//   still what this registry generates.)

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// ---- lane 0: the registry describes itself completely -----------------------
var problems = app.apiProblems();
assert(problems.length === 0,
       "app.apiProblems() is empty over the live module set" +
       (problems.length ? " — got: " + problems.join(" | ") : ""));

// The verb has to be looking at something, or "empty" is meaningless: the
// registry it validated is the one api.verbs() enumerates.
var modules = api.verbs();
assert(modules.length >= 13, "the registry holds the shipped modules: " + modules.length);
var verbCount = 0;
for (var m = 0; m < modules.length; ++m) verbCount += modules[m].verbs.length;
assert(verbCount >= 190, "…and their verbs: " + verbCount);

console.log("PASS: scripting.e2e.introspection");
