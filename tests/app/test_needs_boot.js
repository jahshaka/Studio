// app.test_needs_boot — A TEST PROCESS BOOTS WHAT ITS ROW NEEDS (TEST-NEEDS-1; services/testtier.h,
// worldmodes::applyTestTier). Run by test_needs_boot.sh once per declaration, under
// JAHSHAKA_TEST_TIER / JAHSHAKA_TEST_NEEDS; this script reads what the process says it is
// (app.testTier()) and asserts every switchable feature on a NEW scene and on an OPENED one:
//   photon  named -> on at the World Mode's own Photon tier; else off
//   bloom   named -> the tier's column (Low 0, else 1); else 0
//   smaa    named -> the tier's column; else -1 (off)
//   planar  named -> the tier's column; else a budget of 0
//   ssao    0 either way unless a scene pinned it (every column is 0: SSAO-DOUBLE-1)
// The opened scene was SAVED with every feature on (world.mode(epic) puts every column back):
// the switch runs on every bind, after the reader.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var tt = app.testTier();
console.log("TESTTIER " + J(tt));
assert(tt.tier !== "", "the process has a test tier (" + J(tt) + ")");
var needs = tt.needs, tier = tt.tier;
function named(w) { return needs.indexOf(w) >= 0; }

function check(tag) {
    var p = world.photon();
    var st = world.settings();
    console.log(tag + " photon " + J({ enabled: p.enabled, tier: p.tier }) + " bloom " + st.bloom.value +
                " ssao " + st.ssao.value + " smaa " + st.smaa.value + " planar " + st.planarBudget.value);
    if (named("photon")) {
        assert(p.enabled === true, tag + ": Photon is ON (named)");
        assert(p.tier === tier, tag + ": Photon runs at the World Mode's own tier (" + p.tier + " === " + tier + ")");
    } else {
        assert(p.enabled === false, tag + ": Photon is OFF (not named)");
    }
    var rows = [["bloom", "bloom", 0], ["smaa", "smaa", -1], ["planar", "planarBudget", 0]];
    for (var i = 0; i < rows.length; i++) {
        var w = rows[i][0], id = rows[i][1], off = rows[i][2], r = st[id];
        if (named(w))
            assert(r.value === r.tierValue, tag + ": " + w + " is the tier's column (" + r.value + " === " + r.tierValue + ")");
        else
            assert(r.value === off, tag + ": " + w + " is OFF (" + r.value + " === " + off + ")");
    }
    assert(st.ssao.value === 0, tag + ": SSAO is off (" + st.ssao.value + ")");
}

var guid = project.create("Test Needs " + Date.now());
assert(guid.length > 10, "project.create");
editor.frame(4, 1 / 60);
check("new scene");

// every feature on, saved: the reopen must switch them off again
assert(world.mode({ mode: "epic" }) === "epic", "world.mode(epic) puts every column back");
assert(world.photon().enabled === true && world.settings().bloom.value === 1, "...Photon and bloom on before the save");
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
editor.frame(4, 1 / 60);
check("opened scene");
project.close();
0;
