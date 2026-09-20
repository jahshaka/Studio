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
var store = assets.list({ scope: "store" });
var materials = assets.list({ scope: "store", type: "material" });
var textures = assets.list({ scope: "store", type: "texture" });
console.log("CENSUS_ASSETS=" + store.length);
console.log("CENSUS_MATERIALS=" + materials.length);
console.log("CENSUS_TEXTURES=" + textures.length);
console.log("CENSUS_PROJECTS=" + project.list().length);
