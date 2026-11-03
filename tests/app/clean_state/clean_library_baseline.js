// THE CLEAN LIBRARY BETWEEN ARMS (TEST-STREAMLINE-1 stage 3a item 4, the clean-state law's reset): a pool's BASELINE script
// (run_pool.py --baseline; the app runs it after EVERY arm, green or red) that puts the library back to the boot's: the
// existing verb, app.resetLibrary, with both storages cleared - in a DRIVEN session it re-seeds the primitives
// synchronously and seeds no presets (services/libraryreset.cpp step 5), which is exactly a fresh home's first boot. The
// arm's open project goes with it (discarded, never saved into a catalog on its way out). No second reset mechanism.
var r = app.resetLibrary({ clearAssets: true, clearMaterials: true });
if (!r || r.ok !== true) throw new Error("the clean-state baseline: app.resetLibrary failed: " + JSON.stringify(r));
var store = assets.list({ scope: "store" }).length, projects = project.list().length;
if (store !== 0 || projects !== 0 || project.current())
    throw new Error("the clean-state baseline: the library is not the boot's after the reset (store rows " + store +
                    ", projects " + projects + ", open " + project.current() + ")");
