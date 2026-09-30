// gi_verbs.contact_band — SSAO-DOUBLE-1: NO SECOND OCCLUSION OVER THE GI.
//
// THE DEFECT (the owner's cube, 2026-09-28; the rig check spikes/reflect-leak-2): a cube resting
// on the floor showed a soft dark band on its lower faces toward the contact. It was the post
// chain's SCREEN-SPACE AMBIENT OCCLUSION multiplying the FINISHED colour after the GI had already
// solved visibility: band − control at Epic −11.2 codes on the dark floor and −12.7 on a WHITE
// floor (deeper on white — a multiply that ignores the floor's albedo), where the GI term alone
// read −0.4 / +1.3. A flat unlimited floor subtends the same solid angle at every height of a
// vertical face, so the physics is band ≈ control.
//
// THE FIX: the engine refuses the SSAO passes in every view of a scene whose GI mode is not Off
// (jahshaka::engine::giCarriesOcclusion; world.giStatus().ssaoSuppressed says so), and the World
// row's tier columns are Off. GI OFF — a flat ambient with no bounce — keeps the proxy.
//
// THE CLAIM, on the editor's own picture ("scene" grade), the reflect-leak-2 pose (a 2 m cube on
// the default ground, the camera 5 m from its front face, 1280x720): the band 10-20 cm up the
// front face is within 2 codes of the mid-face control on the dark AND the white floor at Epic
// and at High — even with the SSAO row PINNED on, which the engine still refuses — and with
// Photon off and the row pinned on, the AO still darkens that band by more than 5 codes (the
// proxy works where it should; the arm cannot pass by SSAO having quietly died).
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
var guid = project.create("Contact Band " + Date.now());
assert(guid.length > 10, "project.create");
var G = scene.find("Floor");
assert(!!G, "the default scene has its ground");
var C = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(C.length > 10, "a 2 m cube resting on the ground");
editor.selectNone();
editor.setCamera({ position: { x: 0, y: 1, z: 6 }, lookAt: { x: 0, y: 1, z: 0 }, fov: 45 });

// Probe grids in the image, from the pose: the front face is 5 m away under a 45-degree vertical
// lens over 720 px, so a metre on the face is K pixels and the image centre is 1 m up the cube.
var W = 1280, H = 720, K = 360 / (5 * Math.tan(Math.PI / 8));
function grid(y0, y1) {
    var pts = [];
    for (var y = y0 + 0.015; y < y1; y += 0.035)
        for (var x = -0.575; x <= 0.576; x += 0.05)
            pts.push({ x: (W / 2 + x * K) / W, y: (H / 2 - (y - 1) * K) / H });
    return pts;
}
var BAND = grid(0.10, 0.20), CTRL = grid(0.95, 1.05);
function lum(p) { return 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b; }
function mean(ps) { var s = 0; for (var i = 0; i < ps.length; ++i) s += lum(ps[i]); return s / ps.length; }

function measure(tag, tier, floor, photonOff, pinAo) {
    world.mode({ mode: tier });
    world.photon(photonOff ? { enabled: false } : { enabled: true, tier: tier });
    if (floor === "white") material.set(G, { baseColorMap: "", baseColor: "#f3f3f3" });
    else material.reset(G);
    if (pinAo) world.override({ id: "ssao", value: 1 });
    else world.clearOverride({ id: "ssao" });
    editor.frame(600);
    var n = 600, st = world.giStatus();
    while (!st.giAtRest && n < 2000) { editor.frame(50); n += 50; st = world.giStatus(); }
    var s = editor.screenshot("contact_band_" + tag + ".png", W, H, BAND.concat(CTRL), "scene");
    var band = mean(s.probes.slice(0, BAND.length)), ctrl = mean(s.probes.slice(BAND.length));
    var r = { tag: tag, frames: n, atRest: st.giAtRest, mode: st.mode, ssaoRow: world.settings().ssao.valueId,
              ssaoSuppressed: st.ssaoSuppressed, band: +band.toFixed(1), ctrl: +ctrl.toFixed(1),
              d: +(band - ctrl).toFixed(1) };
    console.log("BAND " + JSON.stringify(r));
    return r;
}
var rows = [
    measure("epic_dark", "epic", "dark", false, false),
    measure("epic_white", "epic", "white", false, false),
    measure("high_dark", "high", "dark", false, false),
    measure("high_white", "high", "white", false, false),
    measure("epic_dark_pinned", "epic", "dark", false, true),
    measure("gioff_dark", "epic", "dark", true, true),
    measure("gioff_white", "epic", "white", true, true),
];
for (var i = 0; i < 5; ++i) {
    var r = rows[i];
    assert(r.atRest, r.tag + ": GI at rest (" + r.frames + " frames)");
    assert(r.mode !== "off" && r.ssaoSuppressed === true,
           r.tag + ": the GI carries the diffuse and the engine says it refuses SSAO");
    assert(Math.abs(r.d) <= 2.0, r.tag + ": the contact band is within 2 codes of the mid-face control (" +
           r.band + " vs " + r.ctrl + ", " + r.d + ")");
}
for (var j = 0; j < 4; ++j)
    assert(rows[j].ssaoRow === "off", rows[j].tag + ": the tier's SSAO column is off (" + rows[j].ssaoRow + ")");
assert(rows[4].ssaoRow === "half", "the pinned row reads half — the ROW honours the pin");
for (var k = 5; k < 7; ++k) {
    var g = rows[k];
    assert(g.ssaoSuppressed === false, g.tag + ": GI off — SSAO is not refused");
    assert(g.d < -5.0, g.tag + ": with GI off the pinned SSAO still darkens the contact band (" + g.d + ")");
}
// THE PANEL'S TRUTH (the settings truth = what renders): at a GI tier the pinned entry names the
// refusal; with Photon off it does not.
function ssaoOptions() {
    var t = world.modeTable().rows, o = null;
    for (var i = 0; i < t.length; ++i) if (t[i].id === "ssao") o = t[i].options;
    return o;
}
world.photon({ enabled: true });
var onGi = ssaoOptions();
assert(onGi && /GI carries occlusion/.test(onGi[1].label) && /GI carries occlusion/.test(onGi[0].label),
       "at a GI tier the row's entries say why (" + onGi[0].label + " | " + onGi[1].label + ")");
world.photon({ enabled: false });
var offGi = ssaoOptions();
assert(offGi && !/GI carries/.test(offGi[1].label), "with GI off they are the plain choices (" + offGi[1].label + ")");
world.clearOverride({ id: "ssao" });
console.log("e2e_contact_band: all ok");
