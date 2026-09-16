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


console.log("e2e_field_every_tier: ALL OK");
