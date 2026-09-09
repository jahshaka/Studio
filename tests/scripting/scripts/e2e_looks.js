// scripting.e2e.looks — the LOOKS STACK's verbs, end to end in the real app
// (SPECS/POST_LOOKS_SPEC.md §4.1, phase 1).
//
// The API-first half of the looks program: every rule the stack has is checked
// through the verbs a user and a script actually call, against a real project
// that is saved, closed and reopened. The PIXEL half is the looks.engine suite;
// these verbs are document verbs, so this runs --headless.
//
// Phase A: the catalogue is real and self-describing.
// Phase B: add / read back / the ordering model.
// Phase C: the rules — unknown look, unknown parameter, one instance per look.
// Phase D: setLook (parameters and enabled), and clamping.
// Phase E: moveLook and removeLook.
// Phase F: undo — every write is exactly one step.
// Phase G: save -> close -> open keeps the stack, in order.
// Phase H: DISTORTION — the world row, its strength, and the shading model,
//          all reached with NO new verb (the registry model's whole point).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Looks " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- phase A: the catalogue ----------------------------------------------
var cat = world.lookCatalogue();
assert(cat.length >= 1, "lookCatalogue has " + cat.length + " look(s)");
var byId = {};
for (var i = 0; i < cat.length; ++i) {
    var d = cat[i];
    assert(typeof d.id === "string" && d.id.length > 0, "look " + i + " has an id");
    assert(typeof d.label === "string" && d.label.length > 0, d.id + " has a label");
    assert(typeof d.doc === "string" && d.doc.length > 0, d.id + " has documentation");
    assert(d.params.length >= 1, d.id + " has parameters");
    assert(d.params[0].id === "amount", d.id + "'s first parameter is 'amount'");
    for (var j = 0; j < d.params.length; ++j) {
        var p = d.params[j];
        assert(typeof p.label === "string" && p.label.length > 0,
               d.id + "." + p.id + " has a label");
        assert(p.min <= p.default && p.default <= p.max,
               d.id + "." + p.id + " default " + p.default + " is inside [" + p.min + ", " + p.max + "]");
    }
    byId[d.id] = d;
}
assert(byId["desaturate"] !== undefined, "the catalogue has 'desaturate'");

// A fresh scene has NO looks — the empty stack is the default and it is what
// makes the renderer's frame byte-identical to a build without this feature.
assert(world.looks().length === 0, "a new scene's looks stack is empty");
assert(world.get().looks.length === 0, "world.get() carries the stack too");

// ---- phase B: add, read back, order --------------------------------------
var added = world.addLook("desaturate", { amount: 0.5 });
assert(added.id === "desaturate", "addLook returned the look");
assert(added.enabled === true, "a look is born enabled");
assert(Math.abs(added.params.amount - 0.5) < 1e-6, "addLook took the parameter");

var stack = world.looks();
assert(stack.length === 1, "the stack has one look");
assert(stack[0].id === "desaturate", "and it is the one added");
assert(stack[0].label === byId["desaturate"].label, "the stack carries the catalogue's label");

// ---- phase C: the rules ---------------------------------------------------
var threw = false;
try { world.addLook("no-such-look"); } catch (e) { threw = true; console.log("   refused: " + e.message); }
assert(threw, "an unknown look id is refused");

threw = false;
try { world.addLook("desaturate", { nonsense: 1 }); } catch (e) { threw = true; }
assert(threw, "an unknown parameter is refused (a typo must never silently do nothing)");

// ONE INSTANCE PER LOOK: every look's parameters live on one shared renderer
// material, so a second copy would be handed the first one's numbers.
threw = false;
try { world.addLook("desaturate"); } catch (e) { threw = true; console.log("   refused: " + e.message); }
assert(threw, "the same look cannot be added twice");
assert(world.looks().length === 1, "and the stack is unchanged by the refusal");

// ---- phase D: setLook ------------------------------------------------------
var changed = world.setLook("desaturate", { amount: 0.25 });
assert(Math.abs(changed.params.amount - 0.25) < 1e-6, "setLook changed the parameter");
changed = world.setLook("desaturate", { enabled: false });
assert(changed.enabled === false, "setLook can switch a look off without losing it");
assert(world.looks().length === 1, "a disabled look stays in the stack");
assert(Math.abs(world.looks()[0].params.amount - 0.25) < 1e-6, "with its settings intact");
world.setLook("desaturate", { enabled: true });

// Clamping: the catalogue's range is the truth and the verb enforces it.
var amountMax = byId["desaturate"].params[0].max;
changed = world.setLook("desaturate", { amount: amountMax + 100 });
assert(Math.abs(changed.params.amount - amountMax) < 1e-6,
       "an out-of-range parameter is clamped to " + amountMax);

threw = false;
try { world.setLook("no-such-look", { amount: 1 }); } catch (e) { threw = true; }
assert(threw, "setLook on a look that is not in the stack is refused");

// ---- phase E: move and remove ---------------------------------------------
// moveLook needs somewhere to move to; with one look in the catalogue the
// single-entry cases are still the ones that must not misbehave.
assert(world.moveLook("desaturate", 0) === true, "moveLook to its own index succeeds");
assert(world.moveLook("desaturate", 99) === true, "an index past the end lands at the end");
assert(world.moveLook("nope", 0) === false, "moveLook on an absent look returns false");
assert(world.looks().length === 1, "and the stack is still one look");

assert(world.removeLook("nope") === false, "removeLook on an absent look returns false");
assert(world.removeLook("desaturate") === true, "removeLook removes it");
assert(world.looks().length === 0, "the stack is empty again");

// ---- phase F: every write RECORDS one undo step ----------------------------
// A script run is ONE open macro, so editor.undo() inside it cannot reach the
// run's own edits — editor.undoState().pushes is the honest measure of "did
// that record a step", exactly as the verb's own documentation says.
var pushes0 = editor.undoState().pushes;
world.addLook("desaturate", { amount: 0.75 });
var pushes1 = editor.undoState().pushes;
assert(pushes1 === pushes0 + 1, "addLook pushed exactly one undo command");
world.setLook("desaturate", { amount: 0.4 });
assert(editor.undoState().pushes === pushes1 + 1, "setLook pushed exactly one");
world.moveLook("desaturate", 0);
assert(editor.undoState().pushes === pushes1 + 1,
       "a move that changes nothing pushes nothing");
world.setLook("desaturate", { amount: 0.75 });
assert(world.looks().length === 1, "the stack still holds one look");
assert(Math.abs(world.looks()[0].params.amount - 0.75) < 1e-6, "with its parameter");

// ---- phase G: save, close, open --------------------------------------------
project.save();
project.close();
project.open(guid);
var reopened = world.looks();
assert(reopened.length === 1, "the stack survives save -> close -> open");
assert(reopened[0].id === "desaturate", "the look survives by id");
assert(Math.abs(reopened[0].params.amount - 0.75) < 1e-6, "and by parameter value");
assert(reopened[0].enabled === true, "and by enabled state");

// ---- phase H: DISTORTION (POST_LOOKS_SPEC.md §5) ---------------------------
//
// The other half of this program, and it is not a look: an object with the
// DISTORTION shading model draws nothing of itself and warps the image behind
// it. It reaches scripts with NO new verb — the world row rides
// world.override/world.settings, its strength rides world.postFx, and the
// shading model is an ordinary material property — which is the point: the
// registry model means a new capability is a table row.
var settings = world.settings();
assert(settings.distortion !== undefined, "the 'distortion' quality row exists");
assert(settings.distortion.available === true, "…and the renderer serves it");
console.log("   distortion resolves to '" + settings.distortion.valueId + "' (" +
            settings.distortion.source + ")");

var fx = world.postFx();
assert(fx.distortionStrength !== undefined, "world.postFx carries distortionStrength");
fx = world.postFx({ distortionStrength: 2.5 });
assert(Math.abs(fx.distortionStrength - 2.5) < 1e-6, "…and it is writable");
fx = world.postFx({ distortionStrength: 999 });
assert(fx.distortionStrength <= 8.0, "…and clamped to the table's range (" +
       fx.distortionStrength + ")");
world.postFx({ distortionStrength: 1 });

var rowState = world.override({ id: "distortion", value: "off" });
assert(rowState.valueId === "off", "the row takes 'off'");
rowState = world.override({ id: "distortion", value: "on" });
assert(rowState.valueId === "on", "…and 'on'");
world.clearOverride({ id: "distortion" });

// The shading model is an ordinary material property, and DISTORTION is now one
// of its values — which is what makes it authorable at all.
var cube = scene.addPrimitive("cube");
assert(cube.length > 10, "a cube to give a distortion material");
var rows = material.properties(cube).rows;
var shadingRow = null;
for (var r = 0; r < rows.length; ++r) if (rows[r].name === "shadingModel") shadingRow = rows[r];
assert(shadingRow !== null, "the material has a shadingModel row");
assert(material.set(cube, { shadingModel: 2 }), "material.set(shadingModel: Distortion)");
assert(material.get(cube).shadingModel === 2, "…and it reads back");
assert(material.set(cube, { shadingModel: 0 }), "…and back to Lit");

// It survives save -> close -> open, like every other scene setting.
world.postFx({ distortionStrength: 0.5 });
world.override({ id: "distortion", value: "on" });
project.save();
project.close();
project.open(guid);
assert(Math.abs(world.postFx().distortionStrength - 0.5) < 1e-6,
       "distortionStrength survives save -> close -> open");
assert(world.settings().distortion.valueId === "on", "…and so does the pinned row");

console.log("scripting.e2e.looks: PASSED");
