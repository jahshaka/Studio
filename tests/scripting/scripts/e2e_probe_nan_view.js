// gi_verbs.probe_nan_view — PROBE-NAN-1 (D4-PHOTON-TIERS): the Probes view under a bright lamp.
//
// THE DEFECT, reproduced on the rig: a point light of intensity 20 placed 0.3 m above the default
// ground turned EVERY irradiance-field probe black in world.setPhotonView("probes"), while
// intensity 2 left them lit. It was not a non-finite value (gi.probe_nan reads the field back:
// finite at 2, 20 and 200, and its DECODED irradiance only grows): the atlas holds the field in the
// voxels' stored units (radiance over the decode multiplier D_max / pi), and the view shaded each
// probe with the raw texel, so a brighter lamp shrank every stored value. The fork now shades a
// probe in the unit the pixel decodes (IfdProbeVisualizer::setColourScale).
//
// THE CLAIM: the probe spheres (the pixels the Probes view changes against the same shot with the
// view off) are LIT at intensity 2, and no darker at 20.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
var guid = project.create("Probe NaN " + Date.now());
assert(guid.length > 10, "project.create");
editor.setCamera({ position: { x: 3, y: 2.5, z: 3 }, lookAt: { x: 0, y: 0.3, z: 0 } });
var W = 48, H = 27, probes = [];
for (var y = 0; y < H; ++y) for (var x = 0; x < W; ++x) probes.push({ x: (x + 0.5) / W, y: (y + 0.5) / H });
function shot(tag) { return editor.screenshot("probe_nan_" + tag + ".png", 256, 144, probes, "scene").probes; }
function lum(p) { return (p.r + p.g + p.b) / 3; }
var lamp = scene.addLight("point", { position: { x: 0, y: 0.3, z: 0 } });
assert(lamp.length > 10, "a point light 0.3 m above the ground");
function probeBrightness(intensity) {
    assert(node.setProperty(lamp, "intensity", intensity), "intensity " + intensity);
    editor.frame(120);
    world.setPhotonView("off");
    editor.frame(4);
    var off = shot("off_" + intensity);
    assert(world.setPhotonView("probes") === true, "the Probes view is on");
    editor.frame(4);
    var on = shot("on_" + intensity);
    world.setPhotonView("off");
    var sum = 0, n = 0;
    for (var k = 0; k < on.length; ++k) {
        var d = Math.max(Math.abs(on[k].r - off[k].r), Math.abs(on[k].g - off[k].g), Math.abs(on[k].b - off[k].b));
        if (d > 8) { sum += lum(on[k]); ++n; }
    }
    console.log("PROBES intensity " + intensity + ": " + n + " probe pixels, mean " +
                (n ? (sum / n).toFixed(1) : "-") + "/255");
    return { n: n, mean: n ? sum / n : 0 };
}
var dim = probeBrightness(2), bright = probeBrightness(20);
assert(dim.n > 20 && bright.n > 20, "the view draws probes at both intensities");
assert(dim.mean > 20, "the probes are LIT at intensity 2 (" + dim.mean.toFixed(1) + ")");
assert(bright.mean >= dim.mean * 0.95,
       "...and NO DARKER at intensity 20 (" + bright.mean.toFixed(1) + " against " +
       dim.mean.toFixed(1) + ") — the defect drew every probe black");
console.log("e2e_probe_nan_view: all ok");
