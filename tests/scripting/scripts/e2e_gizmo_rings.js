// scripting.e2e.gizmo_rings — EVERY ROTATION RING IS CLICKABLE, IN THE REAL
// VIEWPORT (smoke S15; the owner: "sometimes I can't click R, sometimes G or B
// — all three must be click-and-draggable").
//
// gizmo.ring_pick proves the geometry document-side. This proves the SHIPPING
// path: the real app, the real engine viewport, the real editor camera, asked
// through `editor.gizmoHitTest(x, y)` — the same pick a mouse press takes, at a
// pixel, with no synthesized mouse event. From each camera the viewport pixels
// are swept and the rings that answer are collected: all three must be there,
// from every camera, including the ones that put two rings edge-on.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Gizmo Rings " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(cube.length > 10, "cube added at the origin");
assert(editor.select(cube), "cube selected");
assert(editor.setGizmoMode("rotate"), "rotate gizmo active");
editor.frame(2);

var view = editor.viewportState();
assert(view.width > 100 && view.height > 100,
       "viewport is " + view.width + "x" + view.height);

// ---- the verb's shape ------------------------------------------------------
var corner = editor.gizmoHitTest(1, 1);
assert(corner.tolerancePx > 0, "gizmoHitTest reports the pick tolerance (" +
       corner.tolerancePx + " px)");
assert(corner.ring === null, "a viewport corner hits no ring (an explicit null, not undefined)");
assert(corner.distancePx > corner.tolerancePx,
       "and reports how far the nearest ring is (" + corner.distancePx.toFixed(1) + " px)");

// ---- every ring answers, from every camera --------------------------------
//
// The rings are drawn around the selection and are a constant fraction of the
// frame (Gizmo::updateSize), so a coarse grid over the middle of the viewport
// crosses all three. Step 6 px: the pick tolerance is 7, so nothing is missed
// between samples.
function ringsFrom(name, pos) {
    assert(editor.setCamera({ position: pos, lookAt: { x: 0, y: 0, z: 0 } }),
           name + ": camera placed at " + pos.x + "," + pos.y + "," + pos.z);
    editor.frame(2);
    var found = {}, hits = 0, overTolerance = 0;
    var half = Math.min(view.width, view.height) * 0.45;
    var cx = view.width / 2, cy = view.height / 2;
    for (var y = cy - half; y <= cy + half; y += 6) {
        for (var x = cx - half; x <= cx + half; x += 6) {
            var r = editor.gizmoHitTest(x, y);
            if (r.ring === null) continue;
            hits++;
            if (r.distancePx > r.tolerancePx) overTolerance++;
            found[r.ring] = (found[r.ring] || 0) + 1;
        }
    }
    console.log("   " + name + ": x=" + (found.x || 0) + " y=" + (found.y || 0) +
                " z=" + (found.z || 0) + " pixels (" + hits + " hits)");
    assert(overTolerance === 0, name + ": every reported hit is inside the tolerance");
    assert((found.x || 0) > 5 && (found.y || 0) > 5 && (found.z || 0) > 5,
           name + ": ALL THREE rings are clickable");
}

// front / top / right each put two of the three rings EDGE-ON to the camera —
// the configurations in which exactly one ring used to be pickable.
ringsFrom("front  (X and Y edge-on)", { x: 0, y: 0, z: 9 });
ringsFrom("top    (X and Z edge-on)", { x: 0, y: 9, z: 0.01 });
ringsFrom("right  (Y and Z edge-on)", { x: 9, y: 0, z: 0 });
ringsFrom("ground (Y nearly edge-on)", { x: 7, y: 0.35, z: 7 });
ringsFrom("iso",                       { x: 6, y: 6, z: 6 });

// ---- the verb only speaks for the gizmo that picks this way ---------------
assert(editor.setGizmoMode("translate"), "back to the translate gizmo");
var translateProbe = editor.gizmoHitTest(view.width / 2, view.height / 2);
assert(translateProbe.ring === null && translateProbe.distancePx < 0,
       "gizmoHitTest answers null while the rotate gizmo is not the active one");
assert(editor.setGizmoMode("rotate"), "rotate gizmo back");
editor.select(null);
var noSelection = editor.gizmoHitTest(view.width / 2, view.height / 2);
assert(noSelection.ring === null, "and null with nothing selected (no gizmo is drawn)");

console.log("PASSED");
