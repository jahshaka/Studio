// make_ogre_iesprofiles.js — the PORT of Ogre-Next's ApiUsage/IesProfiles
// (SPECS/OGRE_SAMPLES_TAB_SPEC.md §3 row 6).
//
//   cat $TREE/scenes/tools/ogre_pbs_base.js \
//       $TREE/scenes/tools/make_ogre_iesprofiles.js | sed ... > /tmp/port.js
//
// WHAT THE SAMPLE IS FOR. A floor and a wall, four near-identical downlights in
// a row, and each one wearing a different IES PHOTOMETRIC PROFILE — the
// measured intensity-against-angle curve a real luminaire ships with. The
// leftmost has none. Read left to right you see what a profile does: the same
// bulb throws a pool, an arrow, a bollard's skirt or a star.
//
// WHAT THIS PORT CANNOT SHOW (the tile carries the first line):
//
//   * THEIR PROFILES ARE THEIR MEDIA. x-arrow-soft.ies, bollard.ies and
//     star-focused.ies ship with Ogre's samples under their own licence and
//     are not redistributed here. The port ships THREE PROFILES OF OUR OWN,
//     authored for it (scenes/ogre/profiles/*.ies, IESNA:LM-63-1995, synthetic
//     rather than measured, and they say so in their own headers): a 20-degree
//     narrow beam, a soft wide flood, and a BATWING — dark straight down,
//     peaking at 50-60 degrees, which is the shape that lights a wall evenly
//     instead of burning a pool at the foot of the pole, and the reason the
//     format exists.
//   * THEIR POWER PER LIGHT (180 / 18 / 700). Binding a profile here
//     RE-CALIBRATES intensity by the profile's own peak candela, by design, so
//     a profile changes the SHAPE and not the brightness — which is why all
//     four lights in this port carry the same power and the picture is a
//     comparison of shapes rather than of levels.
//   * RENDERER LIMIT, same on both sides: a point light honours a profile only
//     while it casts no shadow; a spot always does. These are spots, like
//     theirs.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/ogre/IesProfiles.zip";
var PREVIEW = TREE + "/scenes/ogre/preview/iesprofiles.png";
var PROFILES = TREE + "/scenes/ogre/profiles/";

assert(project.create("IES Profiles").length > 0, "created the project");

// Their world: no sky, a dark ambient, one wall and one floor, both MirrorLike
// (a polished surface, so the pattern each light throws is readable in the
// specular as well as the diffuse). No 4 x 4 grid here — the sample is the
// light, not the objects.
ogreSky({ sky: "#202020", sunDir: [-1, -1, -1], sunPower: 0.0, skyLight: 1.0 });
ogreFloor(true);   // their `MirrorLike`: plain, polished, no maps

// THEIR WALL: the same 50 x 50 plane pitched 90 degrees and pushed to z = -2,
// which is their setPitch(Degree(90)) verbatim. Our plane primitive's normal is
// +Y and Rx(+90) takes it to +Z, i.e. towards the camera — Rx(-90) would face
// it away and the wall would render as a black slab (measured, 2026-09-14).
var wall = scene.addPrimitive("plane", { position: { x: 0, y: 0, z: -2 },
                                         rotation: { x: 90, y: 0, z: 0 },
                                         scale: { x: 25, y: 1, z: 25 } });
node.rename(wall, "Wall");
assert(material.set(wall, { baseColor: "#b4b4b4", workflow: "Specular", roughness: 0.75,
                            useFresnelColor: true, separateFresnel: false,
                            fresnelColor: srgbHex(0.1, 0.1, 0.1) }),
       "the wall is their MirrorLike too");
// MATTE, NOT MIRROR (measured 2026-09-14): at their MirrorLike roughness the
// four lights read as four specular blobs and the DIFFUSE pools — the thing an
// IES profile actually shapes — wash into one band. 0.75 is the roughness at
// which the narrow beam, the flood and the batwing are three different shapes
// on the floor, which is what the sample is for. Recorded as a deviation.

photonOff();

// FOUR DOWNLIGHTS IN A ROW, their positions: x = (i - 2) * 6, y = 8, z = -0.5,
// pointing straight down, inner 160 / outer 170 degrees (a spot so wide it is
// effectively a bare bulb — which is the point: the PROFILE is what shapes it).
// Our spotCutOff is the HALF angle, so their 170 full is 85 here, which is
// also the renderer's ceiling.
var PROFILE_FILES = [null, "narrow-beam.ies", "wide-flood.ies", "batwing.ies"];
var NAMES = ["No Profile", "Narrow Beam", "Wide Flood", "Batwing"];
for (var i = 0; i < 4; ++i) {
    // ONE POWER FOR ALL FOUR, because binding a profile re-calibrates
    // intensity by the profile's own peak candela — so these four differ in
    // SHAPE and nothing else, which is the whole comparison. Their own powers
    // (pi, 180, 18, 700) differ because their engine does not re-calibrate.
    // Their own pi for all four: their lights hang 8 m over a floor whose
    // falloff curve is ours (the range substitution in the harness header), and
    // at pi the four pools were too dark to compare.
    var id = ogreSpot(NAMES[i], [(i - 2) * 6, 8, -0.5], [0, -1, 0], [1, 1, 1], PI, 85);
    node.setProperty(id, "spotCutOffSoftness", 0.06);   // their 160 inner of a 170 outer
    if (PROFILE_FILES[i]) {
        var guid = assets.importFile(PROFILES + PROFILE_FILES[i]);
        assert(guid && guid.length > 10, "imported " + PROFILE_FILES[i] + " -> " + guid);
        assert(node.setLightProfile(id, guid), NAMES[i] + ": profile bound");
        var read = node.lightProfile(id);
        assert(read.applies === true, NAMES[i] + ": the renderer honours it (" + J(read) + ")");
        assert(read.normalisation > 0, NAMES[i] + ": re-calibrated by its own peak candela (" +
                                       read.normalisation + ")");
    } else {
        assert(node.lightProfile(id).guid === "", NAMES[i] + ": no profile, on purpose");
    }
}

// THEIR CAMERA for this one: (0, 10, 25) looking at the origin — the sample
// overrides the default pose because the four pools are laid out across 24 m.
// THEIR CAMERA is (0, 10, 25) looking at the origin. This port looks DOWN on
// the floor instead — (0, 16, 20) aimed at the wall's foot — because the thing
// to compare is the four POOLS the profiles throw, and from their eye level the
// pools are foreshortened into one wash. Recorded as a deviation; the
// comparison rig's pose file uses their pose on both sides.
ogreCamera([0, 16, 20], [0, 0, -2], 45);
editor.frame(30);
ogreFinish(ARCHIVE, PREVIEW, [{ x: 0.25, y: 0.8 }, { x: 0.5, y: 0.8 }, { x: 0.75, y: 0.8 },
                              { x: 0.5, y: 0.45 }]);
console.log("make_ogre_iesprofiles: PASS");
