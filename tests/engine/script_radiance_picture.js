// script.radiance_picture — THE RADIANCE PICTURE READS THE SCENE's LINEAR RADIANCE (SCRIPT-IN-ENGINE-1 stage 2 B1; the
// lead's RULED 6 and 10). Run by jahengine (the engine's own script host, no Studio, no Qt).
//
// THE LAW (a closed form): a Lambert patch (metalness 0, roughness 1) under a UNIFORM ambient A (the hemisphere's upper
// colour equal to its lower: the same irradiance from every direction), with no light and nothing else in the scene,
// reflects albedo x A per channel - the Lambert integral of a constant radiance over the hemisphere is the radiance itself
// (pi x A x albedo / pi). The view keeps its scene radiance in RGBA16F (PostFxDesc::hdrReadback), so each value read back
// is the exact one rounded to a half float: a relative error of at most 2^-11 (half an ulp of an 11-bit significand) -
// the bound below, per channel, on the patch's mean (every pixel of the patch holds the same value).
// THE CONTROL: the patch's radiance is above zero in every channel (a black picture cannot pass the law's relative bound
// on a zero only because the law's value is not zero; this says so first).
// THE SETTLE: the law is read at frame 60 - its bound: the values at frames 16 and 60 are printed beside it, and they
// must be equal (a value still moving at 60 is no rest).
// RED ONCE (the train's card check; spikes/script-in-engine-1/red-once/script.radiance_picture.txt): readPixels'
// content "radiance" answering the DISPLAY bytes (a scratch arm) - the law fails.

const W = 64, H = 64;
const ALBEDO = [0.8, 0.5, 0.25], AMBIENT = [0.5, 0.5, 0.5];

const view = engine.view.createOffscreen({name: "radiance", width: W, height: H, background: [0, 0, 0, 1]});
const scene = engine.scene.create("radiance");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: AMBIENT, lower: AMBIENT});
engine.view.setPostFx(view, {hdrReadback: true});
const cube = engine.mesh.unitCube(scene);
const patch = engine.node.create(scene);
host.check({ok: engine.node.attachMesh(scene, patch, {mesh: cube,
                    material: engine.material.createPbr(scene, {albedo: ALBEDO, metalness: 0, roughness: 1})}),
            what: "the patch exists"});
// a 4 x 4 slab facing the camera (its +Z face at z = 0.1): the camera 2.9 m in front sees only the face
engine.node.setTransform(scene, patch, {position: [0, 0, 0], scale: [4, 4, 0.2]});
engine.view.setCamera(view, {position: [0, 0, 3], orientation: [0, 0, 0, 1]});

const centre = {x0: W / 4, y0: H / 4, x1: W * 3 / 4, y1: H * 3 / 4};
function read() {
    const r = engine.view.readPixels({view, content: "radiance"});
    host.check({ok: r !== null && r.width === W && r.height === H,
                what: "the radiance picture reads back " + (r ? r.width + " x " + r.height : "nothing: " + host.lastError())});
    const m = engine.radiance.mean(r.radiance, centre);
    engine.radiance.release(r.radiance);
    return m;
}
engine.frames(15);
const at16 = read();
engine.frames(43);
const at60 = read();
console.log("routes at frame 60: " + JSON.stringify(engine.view.routes(view)));

host.check({ok: at60[0] > 0 && at60[1] > 0 && at60[2] > 0,
            what: "THE CONTROL: the patch's radiance is above zero [" + at60.map(v => v.toFixed(6)).join(", ") + "]"});
host.check({ok: at16[0] === at60[0] && at16[1] === at60[1] && at16[2] === at60[2],
            what: "THE SETTLE: frame 16 reads frame 60's radiance [" + at16.map(v => v.toFixed(6)).join(", ") + "]"});
for (let c = 0; c < 3; ++c) {
    const want = ALBEDO[c] * AMBIENT[c], got = at60[c];
    host.check({ok: Math.abs(got - want) <= want * Math.pow(2, -11),
                what: "channel " + "rgb"[c] + ": albedo x ambient = " + want.toFixed(6) + ", read " + got.toFixed(6) +
                      " (ratio " + (got / want).toFixed(6) + "; the half float's bound 2^-11 relative)"});
}
