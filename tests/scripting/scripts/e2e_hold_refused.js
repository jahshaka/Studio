// doc_assets.hold_refused — assets.holdBakes IS A TEST INSTRUMENT (STAGE-CLOSE-TRIAGE-1 read 4): a
// hold left on stalls every import's geometry for the session, so a process booted WITHOUT a test
// tier (this pool declares none) is refused, and the refusal changes nothing.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
assert(app.testTier().tier === "", "this process has no test tier: '" + app.testTier().tier + "'");
var threw = null;
try { assets.holdBakes(true); } catch (e) { threw = String(e); }
assert(threw !== null && threw.indexOf("test instrument") >= 0, "assets.holdBakes(true) is refused: " + threw);
assert(assets.pendingBakes() === 0, "and nothing is held or pending");
console.log("hold_refused: ALL OK");
