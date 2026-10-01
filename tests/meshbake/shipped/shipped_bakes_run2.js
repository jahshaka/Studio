// meshbake.shipped_bakes, RUN 2 (see shipped_bakes.sh): the clip bakes were
// DELETED; the project's open rebuilds them from the clip's own source on a
// worker (a bake BUILD, never a parse) and the character's clip is back.
function assert(cond, msg) {
    if (!cond) { console.log("FAIL: " + msg); throw new Error("assert failed: " + msg); }
    console.log("ok: " + msg);
}
function parses(s) { return s.uiThreadParses + s.uiThreadResourceParses + s.workerParses; }

var mine = project.list().filter(function (p) { return p.name === "Shipped Bakes"; })[0];
assert(mine && mine.guid, "run 1's project is in the library");
app.openStats({ reset: true });
assert(project.open(mine.guid) === true, "project.open");
var s = app.openStats();
console.log("census open: " + JSON.stringify(s));
assert(parses(s) === 0, "the open made NO runtime parse");
assert(s.bakeBuilds >= 1,
       "the open REBUILT the deleted clip bake from its source (" + s.bakeBuilds + " bake build(s))");
var avs = avatar.list();
assert(avs.length >= 1, "the spawned character is in the scene");
var roles = avatar.clipRoles(avs[0].id);
console.log("clip roles: " + JSON.stringify(roles));
assert(roles.clips.length >= 1 && roles.clips[0].length > 0,
       "...with its clip back, read from the REBUILT clip bake (length " +
       (roles.clips.length ? roles.clips[0].length : 0) + " s)");
console.log("PASS shipped_bakes run 2");
