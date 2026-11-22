// rtreflect.odd_size — EVERY PIXEL OF THE REFLECTION IS WRITTEN EVERY FRAME (PHOTON-7 stage 6 commit 7; AB section 3 #19).
// Run by jahengine --test-doors (SCRIPT-IN-ENGINE-1: the engine's own script host, no Studio, no Qt).
//
// THE LAW: the spatial filter writes every full-resolution pixel of the reflection each frame - at half resolution an
// odd-sized target's last column and last row have no trace texel of their own (the trace is floor(size / 2)), and the
// joint-bilateral upsample answers them from the texels beside them; nothing of the resolve's scratch and nothing of an
// older frame survives into the picture.
// THE FIXTURE: an odd 257 x 145 view at High (half-resolution rays: 128 x 72 trace texels), a glossy metal floor
// (perceptual 0.2) reflecting a wide emissive wall. Over the floor's reflection, the remainder column against its
// neighbour must step no more than the neighbour pair beside it does (+ 2 codes), and the same for the remainder row; a
// remainder left to the probe reads the floor without the wall's reflection. Frames: 90.

const W = 257, H = 145;

const view = engine.view.createOffscreen({name: "oddsize", width: W, height: H, background: [0.05, 0.05, 0.06]});
const scene = engine.scene.create("oddsize");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: [0.05, 0.05, 0.06], lower: [0.03, 0.03, 0.03]});

const cube = engine.mesh.unitCube(scene);
const floor = engine.node.create(scene), wall = engine.node.create(scene);
host.check({
    ok: engine.node.attachMesh(scene, floor, {mesh: cube,
            material: engine.material.createPbr(scene, {albedo: [0.9, 0.9, 0.9], metalness: 1.0, roughness: 0.2})}) &&
        engine.node.attachMesh(scene, wall, {mesh: cube,
            material: engine.material.createPbr(scene, {albedo: [0, 0, 0], emissive: [0.9, 0.6, 0.2], roughness: 1.0})}),
    what: "the floor and the wall exist"});
engine.node.setTransform(scene, floor, {position: [0, -0.1, 0], scale: [40, 0.2, 40]});
engine.node.setTransform(scene, wall, {position: [0, 3, -8], scale: [40, 6, 0.5]});

host.check({ok: engine.gi.set(scene, {mode: "vct", quality: "high", bounces: true,
                                      cascades: [{halfSize: 32, resolution: 64, stepCells: 0}]}),
            what: "GI builds"});
engine.view.setPostFx(view, {allowOffscreen: true, ssr: 1});   // High: half-resolution rays
// THE RAY'S OWN ANSWER (PHOTON-7-9A, the lead's ruling R7): the reflection is the hybrid, and this row's law is the ray's
// (the half-resolution trace's dispatch at an odd size), so the screen half answers no texel (the test arm)
host.check({ok: engine.arm({name: "reflect.screenOff", value: 1}),
            what: "the arm reflect.screenOff (the ray's own answer: this row's law is the ray's)"});
// looking down the floor toward the wall, its reflection under the floor's horizon and to the frame's right edge
engine.view.lookAt(view, {eye: [0, 1.6, 6], target: [0, 0.4, -4]});

// THE ROUTE IT MEASURES (URGENT #146): the view's chain exactly, the store live by frame 16 - judged by every frame
engine.view.declareRoutes(view, {gather: true, reflection: true, store: true});

engine.frames(89);
const shot = engine.view.readPixels({view});   // the 90th frame, drawn and read: held natively, by its handle
host.check({ok: shot.width === W && shot.height === H, what: "the picture reads back " + shot.width + " x " + shot.height});
const pic = shot.picture;

// THE FLOOR'S REFLECTION: the lower half of the frame (the wall's reflection is the bright band under the horizon)
const y0 = Math.floor(H / 2) + 4, y1 = H - 2, x0 = Math.floor(W / 4), x1 = Math.floor(W * 3 / 4);
const colRem = engine.picture.columnStep(pic, {axis: "column", a: W - 1, b: W - 2, from: y0, to: y1});
const colPair = engine.picture.columnStep(pic, {axis: "column", a: W - 2, b: W - 3, from: y0, to: y1});
const rowRem = engine.picture.columnStep(pic, {axis: "row", a: H - 1, b: H - 2, from: x0, to: x1});
const rowPair = engine.picture.columnStep(pic, {axis: "row", a: H - 2, b: H - 3, from: x0, to: x1});
const beside = engine.picture.mean(pic, {x0: W - 2, y0, x1: W - 1, y1});
const lum = (beside[0] + beside[1]) / 2;
host.check({ok: lum > 20, what: "the floor beside the remainder reflects the wall (" + lum.toFixed(1) + " codes)"});
host.check({ok: colRem <= colPair + 2,
            what: "the odd remainder COLUMN is written like its neighbours (" + colRem.toFixed(2) + " against " +
                  colPair.toFixed(2) + " + 2)"});
host.check({ok: rowRem <= rowPair + 2,
            what: "the odd remainder ROW is written like its neighbours (" + rowRem.toFixed(2) + " against " +
                  rowPair.toFixed(2) + " + 2)"});
