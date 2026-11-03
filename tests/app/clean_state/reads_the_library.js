// THE LAW FED A VIOLATION (pool.clean_state.law): an arm whose numbers are the library's - a fresh home holds no project.
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
assert(project.list().length === 0, "the library holds no project (" + project.list().length + ")");
assert(assets.list({ scope: "store" }).length === 0, "and no stored asset");
