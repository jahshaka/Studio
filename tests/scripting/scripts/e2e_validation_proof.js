// <pool>.validation_proof — THE LAYER IS REALLY THERE (TESTING-CLEANUP-2 H4). The FIRST arm of every
// pool that runs under the Khronos validation layer (pool.vr_validation, pool.capture_validation):
// such a pool fails on "Validation Error" in its output, which a process whose layer never loaded
// cannot print — so before any arm's silence counts, this asserts the layer was asked for and is
// live on the engine's device (engine.validation(): vkCmdDraw resolves into the layer's library).
// Its negative half (an unlayered run reads requested/active false) is scripting's engine_arms arm.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
var v = engine.validation();
console.log("engine.validation() -> " + JSON.stringify(v));
assert(v.requested === true, "the environment asked the loader for the validation layer");
assert(v.active === true, "the layer is LIVE on the device: vkCmdDraw resolves into " + v.drawEntry);
assert(v.layers.some(function (l) { return l.indexOf("VkLayer_khronos_validation") >= 0; }),
       "...and its library is loaded in the process: [" + v.layers.join(", ") + "]");
console.log("validation_proof: PASS");
