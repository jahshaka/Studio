// Fill a data root with the four things a reset has to take: a PROJECT (with
// a folder on disk), an IMPORTED TEXTURE (a library row, a stored object and a
// sidecar), the thumbnails the catalog stores as blobs, and a pin. The suite
// plants the fifth — an abandoned staging temp — with the shell, because a
// staging temp is by definition something no verb leaves behind on purpose.
//
// FIXTURE_PNG (the picture to import, tests/scripting/fixtures/tiny.png) is
// declared by a one-line prelude the shell prepends — a --script run takes one
// file and no arguments, and a path baked into the checked-in script would be
// a path that only works in one build tree.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var png = FIXTURE_PNG;

var guid = project.create("Reset Me");
assert(guid.length > 10, "a project to lose");
console.log("PROJECT_GUID=" + guid);
console.log("PROJECT_FOLDER=" + project.current().folder);

var texture = assets.importFile(png);
assert(!!texture, "a texture imported into the library and the store");
console.log("TEXTURE_GUID=" + texture);

// A node that uses it, so the project folder holds a real scene and the
// catalog holds the pin — the shape a user's library actually has.
var plane = scene.addPrimitive("Plane");
assert(!!plane, "a node in the scene");
assert(project.save() === true, "the project is written to its folder");

console.log("POPULATED_ASSETS=" + assets.list({ scope: "store" }).length);
console.log("POPULATED_PROJECTS=" + project.list().length);
