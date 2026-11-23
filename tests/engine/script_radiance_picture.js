// script.radiance_picture — THE RADIANCE PICTURE READS THE SCENE's LINEAR RADIANCE (SCRIPT-IN-ENGINE-1 stage 2 B1; the
// lead's RULED 6 and 10). Run by jahengine (the engine's own script host, no Studio, no Qt).
//
// THE LAW (a closed form, DERIVED FROM THE SHADER's OWN PIECES; irisgl at the row's commit): a rough dielectric patch
// (metalness 0, roughness 1, the Default BRDF) seen face-on under a FLAT ambient A (upper == lower), no light, no GI
// (GiParams' default mode Off), nothing else in the scene:
//   (1) upper == lower is the FLAT branch: the SH's band 0 is (upper + lower) / 2 / pi = A / pi, the higher bands 0
//       (irisgl engine/src/OgreScene.cpp:139-148), so the pixel's diffuse environment is envColourD = A / pi
//       (AmbientLighting_piece_ps.any:77, irradianceSH :17-26 - band 0 is sh0 alone);
//   (2) the material's diffuse is albedo x kD, kD = 1 / pi (OgreHlmsPbsDatablock.cpp:494-497, setDiffuse), and the
//       environment's diffuse term multiplies by pi (200.BRDFs_piece_ps.any:352-353): the two cancel;
//   (3) fresnelD = 1: the Default BRDF carries no diffuse-Fresnel flag (PbrParams::brdf "Default", Materials.h:265;
//       OgreMaterials.cpp:274-284 sets it; FresnelHasDiffuse only from FLAG_HAS_DIFFUSE_FRESNEL, OgreHlmsPbs.cpp:853-858;
//       the @else at 200.BRDFs_piece_ps.any:341);
//   (4) the lobe is the NORMALISED DISNEY diffuse's directional albedo DA(v, r), not Lambert's 1
//       (200.BRDFs_piece_ps.any:352-353, JahDiffuseAlbedo_piece_all.any): ef (1 + k a)(1 + k b) over the light's
//       hemisphere, ef = lerp(1, 1/1.51, r), fd90 = 0.5 r + 2 r (V.H)^2, k = fd90 - 1, a = (1 - N.L)^5, b = (1 - N.V)^5;
//   (5) no specular: envColourS gets nothing from the SH (AmbientLighting_piece_ps.any:77 writes envColourD only) and
//       the scene has no probe, no sky, no planar.
// SO L = albedo x A x DA(v, r) / pi per channel. DA is computed HERE, by this script's own quadrature of the lobe's
// definition (4) - never copied from the media. v: the measured rectangle is the frame's central 8 x 8 of 64 x 64 at a
// 45-degree vertical field, so every pixel's view is within 4.2 degrees of the normal (its corner: atan(0.125 x
// tan 22.5 deg x sqrt 2)): 1 - v <= 2.7e-3, and DA's v-dependence there (its r v / 84 term: ef r (1 - v) / 84 <= 2.2e-5;
// its b term (1 - v)^5 ~ 1e-13) is 3.2e-5 of DA - the law takes v = 1, 15 times under its bound.
// THE BOUND: the view keeps its scene radiance in RGBA16F (PostFxDesc::hdrReadback): each value read back is the exact
// one rounded to a half float, a relative error of at most 2^-11 (half an ulp of an 11-bit significand).
// A IS CHOSEN SO EVERY CHANNEL's L IS ABOVE 1: a display byte clips at 1.0 (255), so the red-once arm (the display
// bytes answered for "radiance") cannot pass by luck.
// THE CONTROL: the patch's radiance is above zero in every channel. THE SETTLE: frames 16 and 60 read the same.
// RED ONCE (the train's card check; spikes/script-in-engine-1/red-once/script.radiance_picture.txt): readPixels'
// content "radiance" answering the DISPLAY bytes (a scratch arm) - the law fails.
// UNVERIFIED UNTIL ITS FIRST CARD RUN (the lead's word): that the frame adds nothing else (the route line below says
// what the scene's view ran).

const W = 64, H = 64;
const ALBEDO = [0.8, 0.5, 0.25], AMBIENT = [8, 12, 26], ROUGHNESS = 1;

// THE NORMALISED DISNEY DIFFUSE's DIRECTIONAL ALBEDO by midpoint quadrature of (4): the light's cosine c in n steps
// (weight 2c, the cosine-weighted mean), its azimuth in 2n; the view at cos v in the x-y plane
function disneyAlbedo(v, r, n) {
    const V = [Math.sqrt(Math.max(0, 1 - v * v)), v, 0];
    const ef = 1 + (1 / 1.51 - 1) * r;
    const bv = Math.pow(1 - v, 5);
    let sum = 0;
    for (let i = 0; i < n; ++i) {
        const ct = (i + 0.5) / n, st = Math.sqrt(Math.max(0, 1 - ct * ct));
        for (let j = 0; j < 2 * n; ++j) {
            const ph = 2 * Math.PI * (j + 0.5) / (2 * n);
            const Hx = st * Math.cos(ph) + V[0], Hy = ct + V[1], Hz = st * Math.sin(ph) + V[2];
            const hl = Math.hypot(Hx, Hy, Hz);
            const vdh = hl > 0 ? Math.max(0, (V[0] * Hx + V[1] * Hy + V[2] * Hz) / hl) : 0;
            const k = 0.5 * r + 2 * vdh * vdh * r - 1;
            sum += 2 * ct * (1 + k * Math.pow(1 - ct, 5)) * (1 + k * bv) * ef;
        }
    }
    return sum / (n * 2 * n);
}
const DA = disneyAlbedo(1, ROUGHNESS, 256);
const coarse = disneyAlbedo(1, ROUGHNESS, 128);
// the quadrature's own error, bounded by halving its step: far under the law's 2^-11
host.check({ok: Math.abs(DA - coarse) <= DA * Math.pow(2, -16),
            what: "the lobe's albedo DA(1, " + ROUGHNESS + ") = " + DA.toFixed(6) + " (n 256; n 128 reads " + coarse.toFixed(6) +
                  ": the quadrature holds 2^-16)"});
const WANT = [0, 1, 2].map(c => ALBEDO[c] * AMBIENT[c] * DA / Math.PI);
host.check({ok: WANT.every(x => x > 1),
            what: "every channel's expected radiance is above 1 (a display byte cannot read it): [" +
                  WANT.map(x => x.toFixed(4)).join(", ") + "]"});

const view = engine.view.createOffscreen({name: "radiance", width: W, height: H, background: [0, 0, 0, 1]});
const scene = engine.scene.create("radiance");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: AMBIENT, lower: AMBIENT});
engine.view.setPostFx(view, {hdrReadback: true});
const cube = engine.mesh.unitCube(scene);
const patch = engine.node.create(scene);
host.check({ok: engine.node.attachMesh(scene, patch, {mesh: cube,
                    material: engine.material.createPbr(scene, {albedo: ALBEDO, metalness: 0, roughness: ROUGHNESS})}),
            what: "the patch exists"});
// a 4 x 4 slab facing the camera (its +Z face at z = 0.1): the camera 2.9 m in front sees only the face
engine.node.setTransform(scene, patch, {position: [0, 0, 0], scale: [4, 4, 0.2]});
engine.view.setCamera(view, {position: [0, 0, 3], orientation: [0, 0, 0, 1]});

const centre = {x0: W / 2 - 4, y0: H / 2 - 4, x1: W / 2 + 4, y1: H / 2 + 4};
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
    const want = WANT[c], got = at60[c];
    host.check({ok: Math.abs(got - want) <= want * Math.pow(2, -11),
                what: "channel " + "rgb"[c] + ": albedo x A x DA / pi = " + want.toFixed(6) + ", read " + got.toFixed(6) +
                      " (ratio " + (got / want).toFixed(6) + "; the half float's bound 2^-11 relative)"});
}
