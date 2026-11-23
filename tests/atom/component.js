// atom.component — ATOM IS A COMPONENT OF ITS SCENE (ENGINE-UPGRADE-2 stage 3; irisgl/engine/src/atom/AtomComponent.h).
// Counts only, no picture. Run by jahengine (SCRIPT-IN-ENGINE-1 2f: the engine's own script host, no C++ fixture, no Qt;
// it replaces test_atom_component.cpp, proved equal to it count by count and by its last picture:
// spikes/script-in-engine-1/train/equality/); its twin atom.component_asan runs this script on jahengine_asan.
//
// Two scenes, each drawn by its own view with shadows: A holds six Atom cubes, a floor and a sun; B two cubes, a floor
// and a sun. On every frame the frame monitor records (engine.monitor, "review"):
//   1. THE PASS ORDER, by the compositor's own listing (a record's passes): in each view's workspace the id pass
//      ("Jahshaka atom id") runs ONCE and before the first screen decode ("Jahshaka atom decode*"); the late pass, where
//      the chain has it, between the two; and every caster pass ("Jahshaka atom caster") directly follows a shadow scene
//      pass of the same node and the same map;
//   2. THE WORDS STEP ONCE A FRAME (the `engine.atomwords` stage);
//   3. EACH SCENE ITS OWN STATE: a pass handed only a SceneManager finds its scene bound on the manager, so A's caster cut
//      counts A's casters and B's counts B's (the same sun splits each; A's cut draws more caster instances than B's,
//      both > 0 - THE CONTROL), each scene's split is live and shading its own items, and each view's stats ring reads
//      its own cut back (cutValid);
//   4. OWNERS RETIRE WITH THEIR OBJECTS: B's view destroyed (its stats ring retires through the device's window), then B
//      (its caster state retires likewise) - and A goes on drawing its casters frame after frame, its cut unchanged.
// A device without the split (no id pass) skips: the row says so and asserts nothing.
// The arithmetic the C++ fixture did in float is done here in float (Math.fround): the poses are the same floats.

const f = Math.fround;
const SIZE = 128;
const NO_SHADOW_MAP = 0xFFFFFFFF;   // FramePass::kNoShadowMap (Monitor.h)
const ID_PASS = "Jahshaka atom id";

// a node whose -Y is aimed along `dir`, carrying a directional light of `power` (the C++ helper's float arithmetic:
// -Y rotated onto the direction by the shortest arc; setLight scales the intensity by pi, so it is power / pi)
function addDirectionalLight(scene, dir, power) {
    const n = engine.node.create(scene);
    const dx = f(dir[0]), dy = f(dir[1]), dz = f(dir[2]);
    const len = f(Math.sqrt(f(f(f(dx * dx) + f(dy * dy)) + f(dz * dz))));
    const d = [f(dx / len), f(dy / len), f(dz / len)];
    const from = [0, -1, 0];
    const cross = [f(f(from[1] * d[2]) - f(from[2] * d[1])), f(f(from[2] * d[0]) - f(from[0] * d[2])),
                   f(f(from[0] * d[1]) - f(from[1] * d[0]))];
    const dot = f(f(f(from[0] * d[0]) + f(from[1] * d[1])) + f(from[2] * d[2]));
    let q;
    if (dot < f(-0.999999)) q = [0, 0, 1, 0];
    else {
        const w = f(1 + dot);
        const nn = f(Math.sqrt(f(f(f(f(w * w) + f(cross[0] * cross[0])) + f(cross[1] * cross[1])) + f(cross[2] * cross[2]))));
        q = [f(cross[0] / nn), f(cross[1] / nn), f(cross[2] / nn), f(w / nn)];
    }
    engine.node.setTransform(scene, n, {rotation: q});
    host.check({ok: engine.light.set(scene, n, {type: "directional", colour: [1, 1, 1, 1], intensity: f(power / f(Math.PI))}),
                what: "the sun exists"});
}

function addCube(scene, albedo, roughness) {
    const n = engine.node.create(scene);
    host.check({ok: engine.node.attachMesh(scene, n, {mesh: engine.mesh.unitCube(scene),
                    material: engine.material.createPbr(scene, {albedo, metalness: 0, roughness})}),
                what: "a cube exists"});
    return n;
}

function buildScene(name, cubes) {
    const s = engine.scene.create(name);
    engine.scene.setAmbient(s, {upper: [0.05, 0.05, 0.06, 1], lower: [0.04, 0.04, 0.04, 1]});
    const floor = addCube(s, [0.6, 0.6, 0.6, 1], 0.9);
    engine.node.setTransform(s, floor, {position: [0, -0.55, 0], scale: [12, 0.1, 12]});
    for (let i = 0; i < cubes; ++i)
        engine.node.setTransform(s, addCube(s, [0.7, 0.4, 0.2, 1], 0.6), {position: [-2.5 + i, 0, (i % 2) - 0.5]});
    addDirectionalLight(s, [-0.3, -1, -0.6], 3);
    return s;
}

function stageCount(r, name) { return r.stages.filter(st => st.name === name).length; }

// law 1 for the workspaces whose names hold the tags, the caster rule frame-wide, the words a frame
function readOrder(recs, tags) {
    const o = {frames: 0, good: 0, casters: 0, badCasters: 0, wordsMin: Infinity, wordsMax: 0};
    for (const r of recs) {
        if (r.passes.length === 0) continue;
        ++o.frames;
        const w = stageCount(r, "engine.atomwords");
        o.wordsMin = Math.min(o.wordsMin, w);
        o.wordsMax = Math.max(o.wordsMax, w);
        let frameOk = true;
        for (const tag of tags) {
            let id = -1, late = -1, decode = -1, nId = 0;
            r.passes.forEach((p, i) => {
                if (!p.workspace.includes(tag)) return;
                if (p.pass === ID_PASS) { ++nId; if (id < 0) id = i; }
                if (p.pass === "Jahshaka atom id late" && late < 0) late = i;
                if (p.pass.startsWith("Jahshaka atom decode") && decode < 0) decode = i;
            });
            const ok = nId === 1 && id >= 0 && decode > id && (late < 0 || (late > id && late < decode));
            if (!ok) {
                frameOk = false;
                console.log("   frame " + r.frame + " [" + tag + "]: id " + id + " (x" + nId + ") late " + late + " decode " + decode);
            }
        }
        r.passes.forEach((p, i) => {
            if (p.pass !== "Jahshaka atom caster") return;
            ++o.casters;
            const prev = i ? r.passes[i - 1] : null;
            if (!prev || prev.node !== p.node || prev.workspace !== p.workspace || prev.shadowMapIdx !== p.shadowMapIdx ||
                p.shadowMapIdx === NO_SHADOW_MAP) {
                ++o.badCasters;
                frameOk = false;
            }
        });
        if (frameOk) ++o.good;
    }
    return o;
}

// the monitor over n frames, the first frame's records dropped (a record arrives late: pop across frames)
function monitored(n, last) {
    engine.monitor.set("review");
    engine.frames(1);
    engine.monitor.popRecords();
    if (last) last(n); else engine.frames(n);
    engine.monitor.set("off");
    return engine.monitor.popRecords();
}

function status(st) {
    return "atom items " + st.atomItems + ", screen draws " + st.screenDraws + ", cut " + (st.cutValid ? 1 : 0) +
           ", caster maps " + st.casterMaps + " (instances " + st.casterInstances + ")";
}

function main() {
    const va = engine.view.createOffscreen({name: "atomcompA", width: SIZE, height: SIZE, background: [0, 0, 0, 1]});
    const vb = engine.view.createOffscreen({name: "atomcompB", width: SIZE, height: SIZE, background: [0, 0, 0, 1]});
    const a = buildScene("atomcompA", 6), b = buildScene("atomcompB", 2);
    engine.view.setScene(va, {scene: a});
    engine.view.setScene(vb, {scene: b});
    engine.view.setShadows(va, true);
    engine.view.setShadows(vb, true);
    engine.view.lookAt(va, {eye: [0, 3, 6], target: [0, 0, 0]});
    engine.view.lookAt(vb, {eye: [0, 3, 6], target: [0, 0, 0]});
    engine.frames(12);

    if (!engine.scene.atomDrawStatus(a).on) {
        console.log("the split is off on this device (no id pass) - atom.component skips");
        return;
    }

    const o = readOrder(monitored(4), ["atomcompA", "atomcompB"]);
    const sa = engine.scene.atomDrawStatus(a), sb = engine.scene.atomDrawStatus(b);
    console.log("   " + o.frames + " frames recorded, " + o.good + " in order; " + o.casters + " caster passes (" + o.badCasters +
                " out of place); atomwords/frame " + o.wordsMin + ".." + o.wordsMax);
    console.log("   A: " + status(sa));
    console.log("   B: " + status(sb));
    host.check({ok: o.frames > 0 && o.good === o.frames,
                what: "EVERY FRAME, EACH VIEW: the id pass once, before the first screen decode (the late pass between), and " +
                      "every caster pass right after its map's shadow scene pass"});
    host.check({ok: o.casters > 0, what: "...the caster passes ran (not vacuous)"});
    host.check({ok: o.frames > 0 && o.wordsMin === 1 && o.wordsMax === 1, what: "ATOM'S WORDS STEP ONCE A FRAME (engine.atomwords)"});
    host.check({ok: sa.on && sb.on && sa.atomItems >= 7 && sb.atomItems >= 3 && sa.screenDraws > 0 && sb.screenDraws > 0,
                what: "each scene's split is live and shades its own items through its screen decode"});
    host.check({ok: sa.cutValid && sb.cutValid, what: "each view's stats ring read its own cut back"});
    host.check({ok: sa.casterValid && sb.casterValid && sa.casterMaps > 0 && sa.casterMaps === sb.casterMaps &&
                    sb.casterInstances > 0 && sa.casterInstances > sb.casterInstances,
                what: "EACH SCENE ITS OWN CASTER STATE, THE CONTROL: the same maps each, A's cut counting A's casters (" +
                      sa.casterInstances + ") and B's B's (" + sb.casterInstances + " > 0)"});

    // ---- 4. the owners retire with their objects
    engine.view.destroy(vb);
    engine.frames(3);
    engine.scene.destroy(b);
    engine.frames(12);
    const o2 = readOrder(monitored(3, n => {
        engine.frames(n - 1);                                                           // MEASURING ARM - reverted
        const shot = engine.view.readPixels({view: va});                               // MEASURING ARM - reverted
        console.log("ARM picture fnv1a64 " + engine.picture.hash(shot.picture));     // MEASURING ARM - reverted
        engine.picture.release(shot.picture);                                         // MEASURING ARM - reverted
    }), ["atomcompA"]);
    const sa2 = engine.scene.atomDrawStatus(a);
    console.log("   after B went: " + o2.frames + " frames, " + o2.good + " in order, " + o2.casters + " caster passes; A's caster maps " +
                sa2.casterMaps + " (instances " + sa2.casterInstances + ")");
    host.check({ok: o2.frames > 0 && o2.good === o2.frames && o2.casters > 0 && sa2.casterValid &&
                    sa2.casterMaps === sa.casterMaps && sa2.casterInstances === sa.casterInstances,
                what: "B's view and B gone (their rings retired through the device's window): A draws on, its cut unchanged"});
}

main();
