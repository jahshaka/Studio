// atom.pass_table — THE FULLEST PASS FITS THE PASS-TEXTURE TABLE (BUGS-1 item 1; preflight-atom-3 §A).
//
// The Epic world with sun contact, the 2D cloud layer and the World fog is the fullest pass this
// engine builds: the HlmsAtom passes number their textures after NINE reserved buffer slots and the
// id image, and at the fork's old 64-entry table they asked for 65 (66 once an area light loaded
// the LTC/DFG array): VulkanRootLayout threw at PSO creation and Atom's geometry vanished. This
// script builds exactly that world on the Basic template (an area light created and removed, so
// the LTC array occupies its slot in every PBS-family pass for the rest of the run) and claims:
//   (a) the renderer reports no error (app.engineErrors() empty — the refused layout was one);
//   (b) the visibility buffer is live and its objects are visible (the Density view counts
//       Atom pixels > 0).
// The driver (tests/atom/pass_table.sh) then reads every generated pixel shader the run dumped
// under JAHSHAKA_HLMS_DEBUG_DIR: the highest ogre_t of each must be under the fork's
// NUM_BIND_TEXTURES (read from the fork's header at configure). Frames, never time.

function J(x) { return JSON.stringify(x); }
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Pass table " + Date.now(), { template: "basic" });
assert(guid.length > 10, "project.create (basic) -> " + guid);
assert(world.mode({ mode: "epic" }) === "epic", "the World Mode is Epic");
assert(world.sunContact({ enabled: true }).enabled === true, "sun contact on");
assert(world.clouds({ enabled: true, coverage: 0.5, shadow: 1 }).enabled === true, "the 2D cloud layer on");
assert(world.fog({ enabled: true, density: 0.01 }) === true, "the World fog on");
scene.addPrimitive("sphere", { position: { x: 0, y: 1, z: 0 } });
editor.frame(30);
var L = scene.addLight("area", { position: { x: 3, y: 3, z: 0 } });
editor.frame(6);
assert(node.remove(L) === true, "the area light removed (the LTC array stays bound)");
editor.frame(30);

var st = world.atomStatus();
console.log("atomStatus: on " + st.on + " atomItems " + st.atomItems + " buckets " + st.buckets +
            " screenDraws " + st.screenDraws + " decodeDraws " + st.decodeDraws);
assert(st.on === true && st.atomItems > 0, "the visibility buffer is live and draws objects");
assert(world.setAtomView("density") === true, "the Density view on");
editor.frame(4);
var dens = world.atomStatus().density;
console.log("density: " + J(dens));
assert(dens && dens.pixels > 0, "Atom pixels are visible (" + (dens ? dens.pixels : "none") + ")");
assert(world.setAtomView("off") === true, "the Density view off");
editor.frame(2);

var errs = app.engineErrors();
console.log("engineErrors: " + J(errs).slice(0, 1200));
assert(errs.entries.length === 0, "the renderer reported no error (" + errs.entries.length + " distinct)");
console.log("PASS pass_table");
