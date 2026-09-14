// make_ogre_localcubemaps.js — the PORT of Ogre-Next's ApiUsage/LocalCubemaps
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 2).
//
//   TREE=<absolute path to the source tree>
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/ogre_room.js \
//       $TREE/scenes/tools/make_ogre_localcubemaps.js \
//     | sed "s|@TREE@|$TREE|" > /tmp/port.js
//   cd <a scratch dir>
//   DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/port.js --data-root <a scratch root>
//
// WHAT THE SAMPLE IS FOR. Two glossy chambers joined by a doorway, each with
// its own reflection probe. A cubemap captured in one chamber is WRONG in the
// other — the red wall would appear in a mirror that cannot see it — and a
// probe with no parallax correction smears the whole room into every surface.
// Their demo is the A/B: one probe for the room, or one per chamber, and the
// difference is visible in the floor.
//
// WHAT THIS PORT CANNOT SHOW (the tile says the first line):
//
//   * PROBE PLACEMENT IS GRID-ONLY. They place three probes by hand, each with
//     its own area and its own room shape, and their probe 01 doubles as the
//     "one probe for everything" case. We place probes as a GRID over the
//     measured room (world.gi pccGrid) — the count and the axis are ours to
//     choose, the exact centres are not. This port asks for 1 x 1 x 3, which
//     is their three-probes-along-the-length arrangement at the renderer's
//     own spacing.
//   * THE F6 / F7 / F8 KEYS. Their sample switches probe count, per-pixel vs
//     per-object reflections and cubemap-array vs dual-paraboloid live. Ours
//     are scene settings (world.gi), not keys, and the DPM path is not exposed.
//   * A PROBE ONLY REACHES A MATERIAL THAT CAN REFLECT IT (owner's probe rule,
//     patch 0028). Their room is glossy everywhere at roughness 0.65 and so is
//     this one, so the rule costs the port nothing — but a diffuse repaint of
//     these walls would drop them out of the probe, by design.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/LocalCubemaps.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/localcubemaps.png";

assert(project.create("Local Cubemaps").length > 0, "created the project");

// ---- the world ------------------------------------------------------------
// NO SKY IN THEIRS: LocalCubemaps never calls createAtmosphere, so its
// background is the window's clear colour and its ambient is two hemisphere
// constants — (0.3,0.5,0.7) x 0.075 above, (0.6,0.45,0.3) x 0.04875 below,
// a mean of about 0.031 of radiance. We have no flat ambient any more (owner
// decision D14): ambient IS a Sky Light reading the sky, so the port picks the
// GREY THAT INTEGRATES TO THEIR NUMBER — 0.031 linear is sRGB 48, #303030 —
// and leaves the Sky Light at its honest 1.0. The background then reads as
// their near-black clear colour for the same reason it lights like their
// ambient, which is the whole point of tying the two together.
ogreSky({ sky: "#303030", sunDir: [-1, -1, -1], sunPower: 1.0, skyLight: 1.0 });

// Their floor is part of the room (the 23 boxes), so the port has no separate
// ground — the default Ground would be a second floor under a sealed interior.
removeNode("Ground");

// ---- the room -------------------------------------------------------------
ogreRoom();
ogreRoomLights();
var bounds = ogreRoomBounds();

// ---- the probes -----------------------------------------------------------
// EPIC IS THE HYBRID (VCT + parallax-corrected probes), which is the technique
// their sample is about. The TIER, never its columns: naming mode/quality/
// bounces here would pin them and the scene would open as "Custom".
assert(world.photon({ enabled: true, tier: "epic" }).technique === "vct_pcc_hybrid",
       "Photon Epic — VCT + probes, their technique");
// THREE PROBES DOWN THE LENGTH, like theirs. The grid is a COUNT, not a
// spacing: the renderer measures the room and divides. There is no volume to
// pin and nothing here pins one (a fixed GI volume is banned — bad physics).
assert(world.gi({ pccGrid: { x: 1, y: 1, z: 3 } }), "probe grid 1 x 1 x 3");

editor.frame(20);
var gi = world.giStatus();
log("giStatus: " + J(gi));
assert(gi.mode === "vct_pcc_hybrid", "the renderer is in the hybrid (" + gi.mode + ")");

// THE PROBES ARE REFUSED IN THIS ROOM, TODAY — and that is an ENGINE gap, not
// an authoring mistake (measured 2026-09-14, ~/Developer/spikes/ogre-samples/
// FINDINGS.md, reported to the lead):
//
//   A probe grid is only placed in a space that reads ENCLOSED on at least two
//   axes, and enclosure is decided by finding, on each axis, an outermost
//   facing pair of SLABS that each COVER at least half the room
//   (OgreGi.cpp computeProbeRegion, rules R1/R2). The coverage test is applied
//   to ONE SLAB AT A TIME. This room's left wall is FOUR coplanar panels with
//   three louvred light slots between them, so no single panel covers half the
//   20 m length, no lo-side slab survives the cover test, and the room reads
//   OPEN on X: probeEnclosedAxes 1, probeGridRefused true, probeCount 0.
//
//   PROVED BY ONE BOX: adding a single full-length panel in the same plane as
//   those four takes the reading to 2 and places the grid (18 probes at the
//   default 3x2x3). The port does NOT add it — their room is their room, and a
//   wall with light slots in it is a wall.
//
// The request above stands, so the day the rule reads an assembled wall this
// port gets its probes with no re-authoring. Until then the room is lit by
// VCT + the irradiance field, which is real GI and not nothing.
if (gi.probeGridRefused) {
    log("PROBES REFUSED (engine gap): probeEnclosedAxes=" + gi.probeEnclosedAxes +
        ", probeCount=" + gi.probeCount + " — the left wall is four panels");
} else {
    assert(gi.probeCount > 0, "probes placed (" + gi.probeCount + ")");
}

// ---- the saved camera -----------------------------------------------------
// DELIBERATE DEVIATION from their (0, 5, 15) default, recorded in the FINDINGS:
// the room's far wall stands at z = 14.6, so their own default camera sits
// INSIDE it and their captured reference frame is a wall filling the screen.
// This is the pose their source carries commented out one line below it — the
// middle probe's centre — looking down the length of the room, which is the
// picture the sample is actually about.
editor.setCamera({ position: { x: -0.505, y: 3.4, z: 12.5 },
                   lookAt: { x: -0.505, y: 3.0, z: 0.0 }, fov: 45 });
editor.select(null);
editor.frame(40);

assert(project.save(), "saved");

// ---- the shipped preview --------------------------------------------------
editor.gameView(true);
editor.frame(90);
var shot = editor.screenshot(PREVIEW, 1280, 720,
                             [{ x: 0.5, y: 0.75 }, { x: 0.5, y: 0.4 }], "scene");
log("preview centre: " + J(shot.center) + " probes " + J(shot.probes));
assert(shot.center.r + shot.center.g + shot.center.b > 12, "the preview is not black");
editor.gameView(false);

// ---- the archive ----------------------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + J(out) + ")");
assert(project.close(), "closed");
console.log("make_ogre_localcubemaps: PASS");
