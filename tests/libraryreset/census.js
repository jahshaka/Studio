// THE CENSUS OF A LIBRARY — the same lines, whoever asks.
//
// Printed by the FRESH-data-root arm (a first launch that does nothing else)
// and again by the reset arm after `app.resetLibrary()`. The suite compares
// the two: "the app is exactly a first launch" is not a feeling, it is these
// numbers being equal.
//
// No project is created here on purpose: creating one pins the default
// floor's tile and mints rows, so a census taken with one open would be
// measuring the project, not the library.
//
// THE BUILT-INS ARE SEEDED FIRST, and that is what makes the comparison mean
// anything. A DRIVEN session seeds no presets at launch (by design — a
// background seed moves row counts under a script's feet), so without this
// call both censuses would be all zeros and "the built-ins are re-seeded after
// a reset" would be asserted as 0 == 0. `materials.seedPresets()` is that same
// seed, synchronously, on demand.
materials.seedPresets();
// (`materialRows`, not `materials`: a local of that name would HOIST over the
// api module and make the seed call above a call on undefined.)
var store = assets.list({ scope: "store" });
var materialRows = assets.list({ scope: "store", type: "material" });
var textureRows = assets.list({ scope: "store", type: "texture" });
console.log("CENSUS_ASSETS=" + store.length);
console.log("CENSUS_MATERIALS=" + materialRows.length);
console.log("CENSUS_TEXTURES=" + textureRows.length);
console.log("CENSUS_PROJECTS=" + project.list().length);
