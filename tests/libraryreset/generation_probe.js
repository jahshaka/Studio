// app.library_generation probe: prints what the startup check decided, and makes
// one project so the next boot has something to wipe.
var g = app.libraryGeneration();
console.log("GEN_OUTCOME=" + g.outcome);
console.log("GEN_GENERATION=" + g.generation);
console.log("GEN_ONDISK=" + g.onDisk);
console.log("GEN_WIPED=" + g.wipedAtStartup);
console.log("GEN_PROJECTS=" + project.list().length);
project.create("Generation " + Date.now());
