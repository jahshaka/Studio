// scripting.e2e.world_rows_follow — A WORLD ROW SHOWS WHAT A VERB WROTE
// (SMALL-FIXES-3, the review's general form of its finding).
//
// A world verb (a script, MCP, the console) writes the document without
// touching a widget, and every World section used to keep showing the value
// from before until it was rebound. The column now re-reads the section that
// shows a key on every CHANGED external write (sceneprops::observeWrites /
// notifyExternal). Each arm: one verb, then the section's row read straight
// away through editor.propertyRow — NO gesture between the write and the read.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= (eps || 1e-3); }
function row(key) { return editor.propertyRow({ tab: "world", key: key }); }

project.create("World rows follow " + Date.now());
app.space("editor");
editor.select(scene.root());
editor.propertiesTab({ tab: "world" });
editor.frame(2);

// ---- World: Ray Tracing (the finding) and Gravity -------------------------------
assert(row("rayTracing").text === "Auto", "World > Ray Tracing reads Auto");
world.rayTracing("off");
assert(row("rayTracing").text === "Off", "world.rayTracing('off'): the World row reads Off at once");
world.rayTracing("auto");
assert(row("rayTracing").text === "Auto", "...and Auto again at once");
world.gravity(12.5);
assert(near(row("gravity").value, 12.5), "world.gravity(12.5): the Gravity row reads 12.5 (" + row("gravity").value + ")");

// ---- Fog: Density -----------------------------------------------------------------
world.fog({ enabled: true, density: 0.07 });
assert(row("fogEnabled").value === true, "world.fog({enabled}): the Fog Enabled row is checked");
assert(near(row("fogDensity").value, 0.07), "world.fog({density:0.07}): the Fog Density row reads it (" + row("fogDensity").value + ")");

// ---- Clouds: the switch and Coverage ------------------------------------------------
world.clouds({ enabled: true, coverage: 0.8 });
assert(row("clouds.enabled").value === true, "world.clouds({enabled}): the Clouds row is checked");
assert(near(row("clouds.coverage").value, 0.8, 0.01), "...and Coverage reads 0.8 (" + row("clouds.coverage").value + ")");

// ---- Sky: the type ------------------------------------------------------------------
world.sky("gradient", {});
assert(row("sky.type").text.toLowerCase().indexOf("gradient") >= 0,
       "world.sky('gradient'): the Sky Type row reads it (" + row("sky.type").text + ")");

// ---- Photon: the update budget --------------------------------------------------------
world.gi({ updateBudget: 4 });
assert(near(row("giUpdateBudget").value, 4), "world.gi({updateBudget:4}): the Photon row reads 4 (" + row("giUpdateBudget").value + ")");

// ---- Post Process: Exposure -----------------------------------------------------------
world.postFx({ exposureEv: 1.5 });
assert(near(row("postFx.exposureEv").value, 1.5, 0.01),
       "world.postFx({exposureEv:1.5}): the Exposure row reads it (" + row("postFx.exposureEv").value + ")");

// ---- Shadows: Sun Contact, and Shadow Quality (a World Mode registry row) --------------
world.sunContact({ enabled: true });
assert(row("sunContact.enabled").value === true, "world.sunContact({enabled}): the Sun Contact row is checked");
world.sunContact({ enabled: false });
assert(row("sunContact.enabled").value === false, "...and unchecked again");
world.setShadowResolution(1024);
assert(row("world.shadowResolution").text === "1024",
       "world.setShadowResolution(1024): the Shadow Quality row reads 1024 (" + row("world.shadowResolution").text + ")");

// ---- World Mode: the tier (the registry's own section) ---------------------------------
var tier = world.mode({ mode: "low" });
assert(row("world.mode").text.toLowerCase().indexOf("low") >= 0,
       "world.mode({mode:'low'}): the World Mode row reads it (" + row("world.mode").text + ")");

// ---- VR: Fly Speed --------------------------------------------------------------------
world.vr({ flySpeed: 7 });
assert(near(row("vr.flySpeed").value, 7, 0.01), "world.vr({flySpeed:7}): the Fly Speed row reads 7 (" + row("vr.flySpeed").value + ")");

// ---- A WRITE THAT CHANGES NOTHING RE-READS NOTHING: the row was already right ----------
world.gravity(12.5);
world.fog({ density: 0.07 });
assert(near(row("gravity").value, 12.5) && near(row("fogDensity").value, 0.07),
       "re-asserting the same values leaves the rows as they were");

console.log("PASS world_rows_follow");
