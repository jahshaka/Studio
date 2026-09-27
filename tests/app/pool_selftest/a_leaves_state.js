// pool.runner arm a — leaves a global and an OPEN project behind, on purpose: arm b proves
// the pool's baseline took both away (a fresh JavaScript realm; project.close by the pool).
const POOL_LEAK = "a";
var POOL_GLOBAL = "a";
var guid = project.create("Pool selftest " + Date.now());
if (!guid) throw new Error("assert failed: project.create");
console.log("ok: arm a created a project and left it open");
