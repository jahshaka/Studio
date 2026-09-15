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
assert(corner.handle === null, "a viewport corner hits no ring (an explicit null, not undefined)");
assert(corner.distancePx > corner.tolerancePx,
       "and reports how far the nearest ring is (" + corner.distancePx.toFixed(1) + " px)");

// ---- every ring answers, from every camera --------------------------------
//
// The rings are drawn around the selection and are a constant fraction of the
// frame (Gizmo::updateSize), so a coarse grid over the middle of the viewport
// crosses all three. Step 6 px: the pick tolerance is 7, so nothing is missed
// between samples.
// The camera's view direction, from the quaternion editor.camera() reports:
// q * (0, 0, -1).
function forwardOf(q) {
    return { x: -2 * (q.x * q.z + q.scalar * q.y),
             y: -2 * (q.y * q.z - q.scalar * q.x),
             z: -(1 - 2 * (q.x * q.x + q.y * q.y)) };
}

function ringsFrom(name, pos) {
    assert(editor.setCamera({ position: pos, lookAt: { x: 0, y: 0, z: 0 } }),
           name + ": camera placed at " + pos.x + "," + pos.y + "," + pos.z);
    // It must actually LOOK at the origin — including straight down, where a
    // look-at built against a fixed world +Y used to degenerate (smoke L10
    // item 1: the top camera pitched -36.87 degrees instead of -90, which is
    // why this suite used to nudge it off the pole to z = 0.01).
    var len = Math.sqrt(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z);
    var f = forwardOf(editor.camera().rotation);
    var dot = -(f.x * pos.x + f.y * pos.y + f.z * pos.z) / len;
    assert(dot > 0.99999, name + ": the camera looks at the origin (cos " + dot.toFixed(6) +
           ", view " + f.x.toFixed(4) + "," + f.y.toFixed(4) + "," + f.z.toFixed(4) + ")");
    editor.frame(2);
    var found = {}, hits = 0, overTolerance = 0;
    var half = Math.min(view.width, view.height) * 0.45;
    var cx = view.width / 2, cy = view.height / 2;
    for (var y = cy - half; y <= cy + half; y += 6) {
        for (var x = cx - half; x <= cx + half; x += 6) {
            var r = editor.gizmoHitTest(x, y);
            if (r.handle === null) continue;
            hits++;
            if (r.distancePx > r.tolerancePx) overTolerance++;
            found[r.handle] = (found[r.handle] || 0) + 1;
        }
    }
    console.log("   " + name + ": x=" + (found.x || 0) + " y=" + (found.y || 0) +
                " z=" + (found.z || 0) + " screen=" + (found.screen || 0) +
                " pixels (" + hits + " hits)");
    assert(overTolerance === 0, name + ": every reported hit is inside the tolerance");
    assert((found.x || 0) > 5 && (found.y || 0) > 5 && (found.z || 0) > 5,
           name + ": ALL THREE rings are clickable");
    // THE OUTER GREY RING IS A HANDLE TOO (GIZMO-1 item 2): it frames the
    // three and turns the node about the view direction, and it answers from
    // every camera — it is the one ring that is never edge-on.
    assert((found.screen || 0) > 5, name + ": the outer screen ring is clickable");
}

// front / top / right each put two of the three rings EDGE-ON to the camera —
// the configurations in which exactly one ring used to be pickable.
ringsFrom("front  (X and Y edge-on)", { x: 0, y: 0, z: 9 });
ringsFrom("top    (X and Z edge-on)", { x: 0, y: 9, z: 0 });   // exactly on the pole
ringsFrom("right  (Y and Z edge-on)", { x: 9, y: 0, z: 0 });
ringsFrom("ground (Y nearly edge-on)", { x: 7, y: 0.35, z: 7 });
ringsFrom("iso",                       { x: 6, y: 6, z: 6 });

// ---- THE TRANSLATE GIZMO'S PLANE HANDLES, in the real viewport ------------
//
// GIZMO-1 item 3 (owner report §346: "it helps with the spatial connection for
// the user"). Three squares in the corners between the arrows, picked in pixels
// like the rings. Same sweep, from an iso camera where all three face the
// camera well enough to be drawn.
assert(editor.setGizmoMode("translate"), "back to the translate gizmo");
assert(editor.setCamera({ position: { x: 6, y: 6, z: 6 }, lookAt: { x: 0, y: 0, z: 0 } }),
       "iso camera for the plane handles");
editor.frame(2);
var planes = {}, planeHits = 0, planeOver = 0;
var phalf = Math.min(view.width, view.height) * 0.45;
// How far from the gizmo's centre each handle answers. The cube is at the
// origin and the camera looks at it, so the gizmo's centre IS the middle of the
// viewport — which makes these radii directly comparable (GIZMO-2 round 2).
var nearest = {}, farthest = {};
for (var py = view.height / 2 - phalf; py <= view.height / 2 + phalf; py += 4) {
    for (var px = view.width / 2 - phalf; px <= view.width / 2 + phalf; px += 4) {
        var t = editor.gizmoHitTest(px, py);
        if (t.handle === null) continue;
        planeHits++;
        if (t.distancePx > t.tolerancePx) planeOver++;
        planes[t.handle] = (planes[t.handle] || 0) + 1;
        var dx = px - view.width / 2, dy = py - view.height / 2;
        var r = Math.sqrt(dx * dx + dy * dy);
        if (nearest[t.handle] === undefined || r < nearest[t.handle]) nearest[t.handle] = r;
        if (farthest[t.handle] === undefined || r > farthest[t.handle]) farthest[t.handle] = r;
    }
}
console.log("   translate: xy=" + (planes.xy || 0) + " yz=" + (planes.yz || 0) +
            " xz=" + (planes.xz || 0) + " x=" + (planes.x || 0) + " y=" + (planes.y || 0) +
            " z=" + (planes.z || 0) + " center=" + (planes.center || 0) +
            " (" + planeHits + " hits)");
assert(planeOver === 0, "every reported plane hit is inside the tolerance");
assert((planes.xy || 0) > 5 && (planes.yz || 0) > 5 && (planes.xz || 0) > 5,
       "ALL THREE plane handles are clickable");
assert((planes.x || 0) > 0 && (planes.y || 0) > 0 && (planes.z || 0) > 0,
       "and the three arrows still answer beside them");

// ---- THE SHAFTS INSIDE THE SQUARES BELONG TO THE ARROWS -------------------
//
// GIZMO-2 round 2 (second reader). Each plane square's two inner sides ARE the
// two arrow shafts it lies between, and a square answers 0 px over its whole
// area — so a press on a drawn shaft could not reach the arrow, and since every
// axis lies in TWO squares that both answered 0, the drag went to whichever
// came first in handle order: the object moved in a plane nobody aimed at.
//
// Measured against the squares themselves rather than a pixel guess: the
// squares reach `planeReach` from the centre, so an arrow that answers CLOSER
// than that is answering on the stretch of shaft the squares cover.
var planeReach = Math.max(farthest.xy || 0, farthest.yz || 0, farthest.xz || 0);
console.log("   the squares reach " + planeReach.toFixed(1) + " px from the centre; the " +
            "nearest arrow answer is x=" + (nearest.x || -1).toFixed(1) + " y=" +
            (nearest.y || -1).toFixed(1) + " z=" + (nearest.z || -1).toFixed(1) + " px");
assert(planeReach > 10, "the plane squares answer out to a measurable radius");
assert(nearest.x < planeReach && nearest.y < planeReach && nearest.z < planeReach,
       "every arrow answers INSIDE the squares' own reach — the shafts are the arrows'");
var translateCorner = editor.gizmoHitTest(1, 1);
assert(translateCorner.handle === null, "a viewport corner grabs no translate handle either");

assert(editor.setGizmoMode("rotate"), "rotate gizmo back");
editor.select(null);
var noSelection = editor.gizmoHitTest(view.width / 2, view.height / 2);
assert(noSelection.handle === null, "and null with nothing selected (no gizmo is drawn)");

console.log("PASSED");
