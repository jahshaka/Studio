// THE LAW FED A VIOLATION (pool.clean_state.law): an arm that leaves a project row in the library behind it.
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
var g = project.create(project.nextFreeName("leaves-a-project"), { template: "empty" });
assert(g && g.length > 10, "a project made, and left in the library");
