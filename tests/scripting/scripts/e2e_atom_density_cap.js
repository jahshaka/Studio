// scripting.e2e.atom_density_cap — THE VIEW'S DENSITY CAP IS ONE WORLD-MODE ROW (ATOM-5 stage 1).
//
// The row `atomDensityCap` (Low 4, Medium 3, High 2, Epic 1) beside `atomPixelError`: each World Mode writes
// it, world.atomDensityCap() reads it (a ratio: no LOD bias multiplies it), world.tierTable()'s world list
// carries each mode's column, an override pins it, and the scene file keeps it across save -> close -> open
// (the writer writes it; the reader's absent-key default is the ctor's 1 — trap 10). Document verbs only:
// --headless, no engine, nothing drawn.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Atom Density Cap " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

var want = { low: 4, medium: 3, high: 2, epic: 1 };
var modes = ["low", "medium", "high", "epic"];
for (var i = 0; i < modes.length; ++i) {
    world.mode({ mode: modes[i] });
    var st = world.settings();
    assert(st.atomDensityCap && st.atomDensityCap.value === want[modes[i]] &&
           st.atomDensityCap.tierValue === want[modes[i]],
           "world.mode('" + modes[i] + "') writes the row: " + JSON.stringify(st.atomDensityCap));
    assert(world.atomDensityCap() === want[modes[i]],
           "world.atomDensityCap() at " + modes[i] + " = " + world.atomDensityCap());
}

// THE TIER TABLE carries the column per World Mode, beside the pixel-error row's.
var tt = world.tierTable();
assert(tt.world && tt.world.length === 4, "the tier table maps the four World modes");
for (var k = 0; k < tt.world.length; ++k) {
    var w = tt.world[k];
    assert(w.atomDensityCap === want[w.mode] && w.atomPixelError === want[w.mode],
           "tierTable world '" + w.mode + "': atomDensityCap " + w.atomDensityCap + ", atomPixelError " +
           w.atomPixelError);
}

// AN OVERRIDE PINS IT, and the scene file keeps it (save -> close -> open).
world.mode({ mode: "epic" });
world.override({ id: "atomDensityCap", value: 3 });
assert(world.atomDensityCap() === 3 && world.settings().atomDensityCap.source === "override",
       "an override pins the row at 3 (" + world.atomDensityCap() + ", " + world.settings().atomDensityCap.source + ")");
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
assert(world.atomDensityCap() === 3, "the reopened scene keeps the cap: " + world.atomDensityCap());
world.clearOverride({ id: "atomDensityCap" });
world.mode({ mode: "epic" });
assert(world.atomDensityCap() === 1, "Epic is the exact rule: " + world.atomDensityCap());
project.close();
console.log("PASS: scripting.e2e.atom_density_cap");
