// scripting.e2e.field_every_tier — PHOTON_SPEC §7 E2 (9)'s `gi.field_every_tier`.
//
// ENGINE UP, and in the SCRIPTING tier rather than as an engine suite, for one
// reason: the tier table is the Studio registry's, and an engine-level version
// would have to carry a SECOND COPY of it — which is the defect this area keeps
// producing (audit A F2's five positional tables, deleted by this same lane).
// Here the real tiers are applied through the real verb and the renderer is
// asked what it actually built.
//
// (It is its own file and not a phase of scripting.e2e.photon because that one
// runs --headless: "asked for a field" and "the shader is sampling one" are
// different readings, and only a real renderer knows the second.)

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("Field Every Tier " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// PHOTON_SPEC §7 E2 (9)'s `gi.field_every_tier`, and it lives HERE rather than
// in an engine suite for one reason: the tier table is the Studio registry's,
// and an engine-level version would have to carry a SECOND COPY of it — which
// is the defect this whole area keeps producing (audit A F2's five positional
// tables). Here the real tiers are applied through the real verb.
//
// What it asserts per tier: the irradiance field is BOUND (every tier's column
// is on since E2 (4) made Low a voxel tier), the chain is up, and the field's
// own volume IS cascade 0's box — which is what "the field follows the camera"
// means and what `ifdFollows` counts as you walk.
scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
scene.addPrimitive("cube", { position: { x: 3, y: 1, z: 0 } });
editor.frame(8);
["low", "medium", "high", "epic"].forEach(function (t) {
    world.photon({ enabled: true, tier: t });
    editor.frame(90);
    var st = world.giStatus();
    assert(st.ifdBound === true, t + ": the irradiance field is bound");
    assert(st.ifdProbes > 0, t + ": ...with probes (" + st.ifdProbes + ")");
    assert(st.cascades && st.cascades.length >= 2,
           t + ": the cascade chain is up (" + (st.cascades ? st.cascades.length : 0) + ")");
    var c0 = st.cascades[0];
    // The field's box is cascade 0's box: centre +- halfSize on every axis.
    // A metre of tolerance, because the field's own grid is quantised to its
    // probe spacing and the cascade's to its cell lattice.
    var ok = Math.abs(st.ifdMin.x - (c0.centre.x - c0.halfSize)) < 1.0 &&
             Math.abs(st.ifdMax.x - (c0.centre.x + c0.halfSize)) < 1.0 &&
             Math.abs(st.ifdMin.y - (c0.centre.y - c0.halfSize)) < 1.0 &&
             Math.abs(st.ifdMax.z - (c0.centre.z + c0.halfSize)) < 1.0;
    assert(ok, t + ": the field's volume IS cascade 0's box — " +
               J([st.ifdMin, st.ifdMax]) + " vs centre " + J(c0.centre) + " +-" + c0.halfSize);
    // Low is the tier that changed most: two cascades at 64^3 and NO probes.
    if (t === "low") {
        assert(st.cascades.length === 2, "low: exactly two cascades");
        assert(st.cascades[0].resolution === 64 && st.cascades[1].resolution === 64,
               "low: both at 64^3 — the cell is what decides whether a bounce means anything");
        assert(world.get().gi.mode === "vct", "low: plain VCT, no probe grid");
    }
});


// ---------------------------------------------------------------------------
// AND THE FIELD'S EDGE IS A RAMP, NOT A NOTCH (SQUARE-1, 2026-09-17; the owner's
// smoke of push #42: "a square lighting effect around the scene in an empty
// scene — light hitting a select area; not centred around the scene").
//
// The square is real and it is cascade 0's box, which is 10 m on the CAMERA, so
// it travels with the eye. Most of it is the CONE term stepping at the chain's
// own hop (measured on a new default scene, straight down from 4 m at Epic,
// plain grade: the cones alone read 110/255 outside cascade 0 and 98-100 inside
// it), and the irradiance field CANCELS most of that — it is not the cause.
// What the field did add was the sharpest feature of the edge: a dark LINE along
// the face, because its confidence ramp used to live INSIDE the box, so every
// pixel in the last half probe spacing was handed the cone value from inside
// cascade 0 while the field was still fading up. The profile fell off the
// outside value to the inside cone value and climbed back — 6-9/255 deep on the
// coarse axes, a notch under BOTH sides.
//
// This asserts the SHAPE, which is what an eye finds: the band across the face
// lies between its two sides. It does NOT assert that the two sides agree —
// they do not, by 4-6/255, because the field integrates cascade 0 and throws the
// escape away while the cones integrate the chain with the sky as their escape,
// and that one is the plan's single voxel reader, not a media constant.
//
// THE INSTRUMENT: a straight-down camera over the default floor, so every probed
// pixel is the same surface with the same normal and any shape in the row is a
// boundary; the floor's checker is taken off for the same reason; the map from
// image column to world metres is exactly linear for a straight-down pinhole
// (x = (2u - 1) * h * tan(fov/2), square shot). The `plain` grade, because this
// is a measurement.
(function faceIsARamp() {
    var H = 4, FOV = 120, HALF = H * Math.tan(FOV / 2 * Math.PI / 180);
    var ground = scene.find("Ground");
    assert(!!ground, "the default floor is there to measure on");
    material.set(ground, { baseColorMap: "" });
    material.set(ground, { baseColor: "#B3B3B3" });
    world.photon({ enabled: true, tier: "epic" });
    editor.setCamera({ position: { x: 0, y: H, z: 0 },
                       lookAt: { x: 0.0001, y: 0, z: 0 }, fov: FOV });
    editor.frame(300);                       // frames, never a wall clock
    var st = world.giStatus();
    var c0 = st.cascades[0];
    var faceX = c0.centre.x + c0.halfSize, faceZ = c0.centre.z + c0.halfSize;

    // Probe points at chosen world offsets from each face: the two sides, then
    // the band across it.
    var SIDES_OUT = [0.20, 0.30, 0.40], SIDES_IN = [-0.20, -0.30, -0.40];
    var BAND = [-0.15, -0.10, -0.05, 0.0, 0.05, 0.10, 0.15];
    var offs = SIDES_OUT.concat(SIDES_IN).concat(BAND);
    var probes = [];
    offs.forEach(function (d) { probes.push({ x: 0.5 + (faceX + d) / (2 * HALF), y: 0.5 }); });
    offs.forEach(function (d) { probes.push({ x: 0.5, y: 0.5 + (faceZ + d) / (2 * HALF) }); });
    var shot = editor.screenshot("field_face.png", 1024, 1024, probes, "plain");
    var lum = shot.probes.map(function (p) { return (p.r + p.g + p.b) / 3; });
    function mean(a) { var s = 0; a.forEach(function (v) { s += v; }); return s / a.length; }
    [["x", faceX, 0], ["z", faceZ, offs.length]].forEach(function (axis) {
        var base = axis[2];
        var out = mean(lum.slice(base, base + 3));
        var ins = mean(lum.slice(base + 3, base + 6));
        var band = lum.slice(base + 6, base + 6 + BAND.length);
        var bandMin = Math.min.apply(null, band);
        var lo = Math.min(out, ins);
        console.log("face " + axis[0] + " at " + axis[1].toFixed(2) + " m: outside " +
                    out.toFixed(1) + " inside " + ins.toFixed(1) + " band [" +
                    band.map(function (v) { return v.toFixed(0); }).join(" ") + "] min " +
                    bandMin.toFixed(1) + " (" + (bandMin - lo).toFixed(1) + "/255 against the darker side)");
        // 2/255 of slack: a probe block is a 5x5 average and the boundary itself
        // is a blend. The notch this refuses measured 6/255 on z and 2-4 on x.
        assert(bandMin >= lo - 2.0,
               "the band across cascade 0's " + axis[0] + " face lies between its two sides, " +
               "not in a notch under both");
    });
})();

console.log("e2e_field_every_tier: ALL OK");
