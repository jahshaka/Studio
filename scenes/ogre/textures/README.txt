THE PORTS' OWN TEXTURE SET (SPECS/OGRE_SAMPLES_TAB_SPEC.md)

512 x 512 copies of three of Jahshaka's shipped material presets — marble_tile,
stone and brick_ground (app/content/materials/presets/) — resampled from their
1024 x 1024 originals with a Lanczos filter.

WHY THEY EXIST, and it is not an art decision.

Every sample archive is SELF-CONTAINED by the portability law: it carries the
bytes of every image its scene uses, so a user with an empty library can open
it. Eight of the thirteen Ogre-sample ports stand on the same base scene (a
marble plaza under rock-and-marble solids), so at full resolution each of those
eight archives carried the same ~8.7 MB of preset maps and the folder came to
97 MB. At 512 it is ~2.7 MB per archive and about 35 MB in total, and at the
distances these scenes are photographed from — a 50 m plaza seen from 15 m,
tiled four times across — the two are indistinguishable.

Ogre's own media is NOT here and is never redistributed by a port: their `Rocks`
(Rocks_Diffuse/Normal/Spec.tga) and `Marble` (MRAMOR6X6.jpg, MRAMOR-bump.jpg)
stay in their tree. These are ours.

Regenerate them with the six-line PIL script recorded in
~/Developer/spikes/ogre-samples/FINDINGS.md; do not hand-edit.
