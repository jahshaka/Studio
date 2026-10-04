// app.start_in_vr's payload (VR-SETTING-1): one script, three boots. It reads
// what decided THIS run and asserts the arm that source names, then leaves the
// preference where the next boot expects it. start_in_vr.sh checks the order.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
var s = vr.startInVr();
console.log("start_in_vr: source=" + s.source + " thisRun=" + s.thisRun + " on=" + s.on +
            " available=" + s.available + " notice=" + JSON.stringify(s.notice));
var NOTICE = "VR is on in Settings but no headset was found: start WiVRn, connect the headset, then restart";
var threw = false;
try { vr.startInVr("yes"); } catch (e) { threw = String(e).indexOf("true or false") >= 0; }
assert(threw, "vr.startInVr refuses a value that is not true or false");

if (s.source === "setting" && s.thisRun) {
    // ARM 1: a fresh data root (the preference's default), no OpenXR runtime.
    assert(s.on === true, "Start in VR is ON by default");
    assert(s.available === false, "no runtime: the boot came up without VR (the desktop route)");
    assert(s.notice === NOTICE, "the one notice says what to do");
    assert(app.columns().space !== undefined, "the editor answers on the desktop route");
    var off = vr.startInVr(false);
    assert(off.on === false && off.thisRun === true, "writing false changes the NEXT launch, not this one");
} else if (s.source === "setting") {
    // ARM 2: the preference OFF.
    assert(s.on === false, "the preference persisted OFF");
    assert(s.available === false && s.notice === "", "an OFF boot never asked and says nothing");
    assert(vr.startInVr(true).on === true, "writing true back");
} else if (s.source === "JAHSHAKA_VR=0") {
    // ARM 3: the runners' form over a preference that is ON.
    assert(s.on === true && s.thisRun === false, "JAHSHAKA_VR=0 wins over the preference");
    assert(s.notice === "", "a forced-off boot says nothing");
} else {
    throw new Error("start_in_vr: unexpected source " + s.source);
}
