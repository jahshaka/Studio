// pool.runner arm b — the baseline an arm starts from: no global of an earlier arm, no
// project open. Redeclaring arm a's `const` is itself the proof of the fresh realm — in a
// shared realm it is a SyntaxError before the first line runs.
const POOL_LEAK = "b";
if (typeof POOL_GLOBAL !== "undefined") throw new Error("assert failed: arm a's global survived");
if (project.current()) throw new Error("assert failed: a project is still open");
console.log("ok: arm b starts in a fresh realm with no project open");
