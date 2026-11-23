// lights.falloff — POINT / SPOT FALLOFF AND THE SPOT CONE (IMAGE-1; F-S1's fix 5). Run by jahengine (SCRIPT-IN-ENGINE-1
// 2f: the engine's own script host, no C++ fixture, no Qt; it replaces test_light_falloff.cpp, proved equal to it picture
// by picture: spikes/script-in-engine-1/train/equality/).
//
// THE FALLOFF, A CLOSED FORM. A point or spot light of intensity I lights a surface at distance d with the inverse square
// law from its source radius rSrc, windowed to zero at its authored range R (Karis 2013, "Real Shading in Unreal Engine 4",
// eq. 9; the source clamp is Frostbite's):
//     E = I / max(d^2, rSrc^2) x saturate(1 - (d / R)^4)^2
// - the renderer's own statement is the fork's JahBrdf piece (jahLightAttenuation), the C++ suites' tests/support/
// lightfalloff.h. WHY IT IS THE LAW HERE: a small rough patch faces the light, the light straight above it at d, the camera
// straight above both: the BRDF, the albedo, the angle and pi are one constant, so the patch's radiance L(d) =
// K x falloff(d). K is read at d = 1 m (ref = L(1) / falloff(1)) and every other d must read ref x falloff(d) within 2 %
// between 1 m and R / 2 (the window is 0.879 at R / 2, so a bare 1 / d^2 is NOT what is asserted: the stated law is);
// inside the source radius the light stops getting brighter (the max); near the range the window is the stated one
// (0.9 R within 5 %), it fades smoothly (0.98 R under a fifth of 0.9 R) and NOTHING is lit past R (exactly 0).
// MEASURED IN FLOAT: the view's post chain keeps the scene's linear radiance (hdrReadback) and readPixels' content
// "radiance" reads it - the patch's centre pixel, the mean of r g b.
//
// THE CONE (fix 5). The spot's angle is a HALF angle everywhere in the document (the editor's cone wire has radius
// range x tan(half)); the disc it lights on the floor 5 m below must have the wire's radius: the floor at 0.75 of the
// wire is lit (more than a quarter of the centre: before the fix it was black - the cone half as wide), at 1.40 of the
// wire it is not (under 5 % of the centre). The softness (0 hard, 0.9 nearly all penumbra) and the falloff exponent (1
// against 4: the light concentrated into the core) each darken the shoulder as they say. Read on the display bytes
// (the plain instrument: an offscreen view without the chain), the centre pixel's mean of r g b in codes / 255.
// THE CONTROLS: ref > 0 (the light lights the patch at all) and the spot's centre > 0.05.
// TWO VIEWS: engine.view.setScene cannot detach a view from its scene, so each case binds its own view; the first is
// destroyed with its scene before the second is made (one view at a time, as the C++ fixture drew).
// The arithmetic the C++ fixture did in float is done here in float (Math.fround): the poses are the same floats.

const f = Math.fround;
const SIZE = 128, C = SIZE / 2;

function lightFalloff(d, range, sourceRadius) {
    const x = range > 0 ? d / range : 1;
    const w = Math.min(1, Math.max(0, 1 - x * x * x * x));
    return (w * w) / Math.max(d * d, sourceRadius * sourceRadius);
}

// a 1 x 1 m unit cube patch: its PBR material (rough dielectric), the node scaled as given
function addPatch(scene, scale) {
    const n = engine.node.create(scene);
    host.check({ok: engine.node.attachMesh(scene, n, {mesh: engine.mesh.unitCube(scene),
                    material: engine.material.createPbr(scene, {albedo: [0.9, 0.9, 0.9, 1], metalness: 0, roughness: 0.9})}),
                what: "the probe patch exists"});
    engine.node.setTransform(scene, n, {scale});
    return n;
}

function pointRangeCase() {
    const R = 6, SRC = f(0.1);   // LightDesc's default sourceRadius (Lights.h), the light below leaves it at its default
    console.log("-- point light, range " + R.toFixed(1) + ": the inverse square law, windowed to the range");
    const view = engine.view.createOffscreen({name: "falloff", width: SIZE, height: SIZE, background: [0, 0, 0, 1]});
    const s = engine.scene.create("falloff_point");
    engine.view.setScene(view, {scene: s});
    engine.scene.setAmbient(s, {upper: [0, 0, 0, 1], lower: [0, 0, 0, 1]});
    engine.view.setPostFx(view, {hdrReadback: true});   // the scene's linear radiance, before any grade
    addPatch(s, [1.2, 0.05, 1.2]);
    const lamp = engine.node.create(s);
    host.check({ok: engine.light.set(s, lamp, {type: "point", colour: [1, 1, 1, 1], intensity: 4, range: R,
                                               castShadows: false}),
                what: "the point light exists"});

    // the light straight above the patch's top face (y = 0.025: a 0.05-high box at the origin) at d; frame 3 read
    function radianceAt(d) {
        engine.node.setTransform(s, lamp, {position: [0, f(f(0.025) + f(d)), 0]});
        engine.view.lookAt(view, {eye: [0, 0.6, 0.001], target: [0, 0, 0]});
        engine.frames(2);
        const r = engine.view.readPixels({view, content: "radiance"});
        const m = engine.radiance.mean(r.radiance, {x0: C, y0: C, x1: C + 1, y1: C + 1});
        engine.radiance.release(r.radiance);
        return f(f(f(m[0] + m[1]) + m[2]) / 3);
    }

    const ref = radianceAt(1) / lightFalloff(1, R, SRC);
    let worst = 0;
    for (const d of [1, 1.5, 2, 2.5, 3]) {
        const e = radianceAt(d), law = ref * lightFalloff(d, R, SRC), bare = ref / (d * d);
        console.log("   d " + d.toFixed(1) + " m: radiance " + e.toFixed(5) + "  law " + law.toFixed(5) + " (" +
                    (100 * (e / law - 1)).toFixed(2) + " %)  1/d^2 " + bare.toFixed(5) + " (" +
                    (100 * (e / bare - 1)).toFixed(2) + " %)");
        worst = Math.max(worst, Math.abs(e / law - 1));
    }
    host.check({ok: ref > 0, what: "THE CONTROL: the light lights the patch at all (ref " + ref.toFixed(6) + ")"});
    host.check({ok: worst < 0.02, what: "between 1 m and R/2 the light follows I / d^2 x the window within 2 % (worst " +
                                        (100 * worst).toFixed(2) + " %)"});

    // INSIDE THE SOURCE: no brighter than at the source radius
    const atSrc = radianceAt(SRC), inside = radianceAt(f(SRC * f(0.5)));
    console.log("   d = rSrc " + atSrc.toFixed(5) + ", d = rSrc/2 " + inside.toFixed(5));
    host.check({ok: Math.abs(inside / atSrc - 1) < 0.02,
                what: "inside the source radius the light stops getting brighter (1 / max(d^2, rSrc^2))"});

    // THE WINDOW: smooth to zero at R, nothing past it
    const d90 = f(R * f(0.9)), d98 = f(R * f(0.98)), d110 = f(R * f(1.1));
    const at90 = radianceAt(d90), at98 = radianceAt(d98), beyond = radianceAt(d110);
    const law90 = ref * lightFalloff(R * 0.9, R, SRC);   // the law at 0.9 R exactly (the light sits at the float d90)
    console.log("   d 0.9R " + at90.toFixed(6) + " (law " + law90.toFixed(6) + ")  0.98R " + at98.toFixed(6) +
                "  1.1R " + beyond.toFixed(6));
    host.check({ok: Math.abs(at90 / law90 - 1) < 0.05, what: "the window is the stated one near the range (0.9 R)"});
    host.check({ok: at98 < at90 * 0.2 && at98 >= 0, what: "the light fades smoothly into the range circle"});
    host.check({ok: beyond === 0, what: "NOTHING is lit past the authored range"});

    engine.view.destroy(view);
    engine.scene.destroy(s);
}

function spotConeCase() {
    const halfDeg = 40, height = 5;
    const wire = f(height * f(Math.tan(f(f(halfDeg * f(3.14159265)) / 180))));
    console.log("-- spot light, half angle " + halfDeg + " deg at height " + height.toFixed(1) + ": wire radius " +
                wire.toFixed(2));
    const view = engine.view.createOffscreen({name: "falloff_spot", width: SIZE, height: SIZE, background: [0, 0, 0, 1]});
    const s = engine.scene.create("falloff_spot");
    engine.view.setScene(view, {scene: s});
    engine.scene.setAmbient(s, {upper: [0, 0, 0, 1], lower: [0, 0, 0, 1]});
    const SCALE = [0.8, 0.05, 0.8];
    const probe = addPatch(s, SCALE);
    const lamp = engine.node.create(s);
    // identity rotation: an engine light shines down -Y; 5 m away, 20 / 25 = 0.8 at the centre, no saturation
    engine.node.setTransform(s, lamp, {position: [0, height, 0]});
    const spot = {type: "spot", colour: [1, 1, 1, 1], intensity: 20, range: height * 3, spotAngleDegrees: halfDeg,
                  spotSoftness: 0.15, spotFalloff: 1, castShadows: false};
    host.check({ok: engine.light.set(s, lamp, spot), what: "the spot light exists"});

    // the patch at (x, 0, 0), the camera straight down onto it, close enough that it fills the frame; frame 3 read
    function probeAt(x) {
        engine.node.setTransform(s, probe, {position: [x, 0, 0], scale: SCALE});
        engine.view.lookAt(view, {eye: [x, 3, 0.001], target: [x, 0, 0]});
        engine.frames(2);
        const r = engine.view.readPixels({view});
        const m = engine.picture.mean(r.picture, {x0: C, y0: C, x1: C + 1, y1: C + 1});
        engine.picture.release(r.picture);
        return f(f(f(f(m[0] / 255) + f(m[1] / 255)) + f(m[2] / 255)) / 3);
    }
    function relight(softness, falloff) {
        spot.spotSoftness = softness;
        spot.spotFalloff = falloff;
        host.check({ok: engine.light.set(s, lamp, spot), what: "the spot takes softness " + softness + ", falloff " + falloff});
    }

    const centre = probeAt(0), in75 = probeAt(f(wire * f(0.75))), out140 = probeAt(f(wire * f(1.4)));
    console.log("   luminance:  centre " + centre.toFixed(3) + "   0.75x wire " + in75.toFixed(3) + "   1.40x wire " +
                out140.toFixed(3));
    host.check({ok: centre > 0.05, what: "THE CONTROL: the spot lights its own centre (" + centre.toFixed(3) + ")"});
    host.check({ok: in75 > centre * 0.25,
                what: "the floor 75% of the way to the WIRE is lit (before: black - the cone was half as wide)"});
    host.check({ok: out140 < centre * 0.05, what: "...and outside the wire it is not"});

    // the softness: a hard-edged spot lights the same disc with a brighter shoulder than a soft-edged one
    relight(0, 1);
    const hard95 = probeAt(f(wire * f(0.95)));
    relight(0.9, 1);
    const soft95 = probeAt(f(wire * f(0.95)));
    console.log("   at 0.95x wire:  softness 0.0 -> " + hard95.toFixed(3) + "   softness 0.9 -> " + soft95.toFixed(3));
    host.check({ok: hard95 > soft95, what: "softness widens the penumbra (0 is a hard edge, 0.9 is nearly all of it)"});

    // the falloff exponent: a higher one concentrates the light into the core, so the shoulder darkens
    relight(0.5, 1);
    const fall1 = probeAt(f(wire * f(0.85)));
    relight(0.5, 4);
    const fall4 = probeAt(f(wire * f(0.85)));
    console.log("   at 0.85x wire:  falloff 1.0 -> " + fall1.toFixed(3) + "   falloff 4.0 -> " + fall4.toFixed(3));
    host.check({ok: fall4 < fall1, what: "the falloff exponent reaches the renderer"});

    engine.view.destroy(view);
    engine.scene.destroy(s);
}

pointRangeCase();
spotConeCase();
