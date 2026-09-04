---
name: jahshaka-particles
description: Author particle emitters in Jahshaka — presets, emission rate, shape, over-life colour and scale ramps, textures and the deterministic stepping that makes them observable. Use when the user asks for fire, smoke, rain, snow, sparks, embers, dust or any emitter effect.
version: 1
---

# Particles in Jahshaka

Emitters are scene nodes; the ENGINE simulates them and the document holds only
authoring parameters. That means an emitter starts running on the next rendered
frame with no tick from anyone — and it also means the ramps and the scalars
live in two different places (see below).

## Start from a recipe

```js
var fx = scene.addParticles("fire", { position: {x: 0, y: 0, z: 0} });
particles.presets();          // custom, fire, embers, smoke, rain, snow,
                              // steadyFlow, sparks
particles.preset(fx, "smoke");// re-stamp a whole recipe onto an existing node:
                              // rate, velocity, lifetime, size, cone, forces,
                              // turbulence, blend mode, quota and both ramps
```

`particles.preset` keeps the node's identity and transform — it replaces the
look, not the object. Start from the nearest preset and adjust; building an
emitter from defaults is a much longer road.

## Scalars: node.setProperty

`node.properties(fx)` lists every writable row with its current value. The
useful ones:

| key | what it does |
|---|---|
| `particlesPerSecond` | emission rate |
| `maxParticles` | quota (int) — the hard cap |
| `particleScale`, `scaleError` | size and its randomisation |
| `lifeLength`, `lifeError` | lifetime in seconds and its randomisation |
| `speed`, `speedError` | launch speed and its randomisation |
| `coneAngle` | spread of the launch cone, degrees |
| `gravityComplement` | how much world gravity is cancelled |
| `wind`, `turbulence` | vec3 drift and chaotic motion |
| `extents`, `innerExtents` | vec3 spawn volume (NOT the node's scale) |
| `shape` | `point`, `box`, `cylinder`, `ellipsoid`, `hollowEllipsoid`, `ring` |
| `orientation` | `billboard`, `stretchedCommon`, `stretchedVelocity`, `perpendicularCommon`, `perpendicularVelocity` |
| `rotationSpeedMin/Max`, `randomRotation` | spin |
| `blendMode` | true = additive (fire, sparks), false = alpha |
| `alphaHash` | hashed alpha instead of blending |
| `emitColourStart`, `emitColourEnd` | the two-colour fallback when there is no ramp |
| `dissipate`, `dissipateInv` | fade out over life (and inverted) |
| `burstDuration`, `burstRepeatDelay`, `startDelay` | bursts and delays |

```js
node.setProperty(fx, "particlesPerSecond", 120);
node.setProperty(fx, "coneAngle", 25);
node.setProperty(fx, "extents", {x: 0.5, y: 0.1, z: 0.5});
```

**The node's SCALE does not resize anything.** The spawn volume is `extents`
and the particle size is `particleScale`; both are numbers, not transforms.

**`texture` is read-only here.** Binding an image needs the asset manager, so
it has its own verb:

```js
node.setParticleTexture(fx, textureGuid);   // "" clears it
node.particleTexture(fx);                   // {path}
```

## Ramps: the particles module

Colour-over-life and scale-over-life are lists, which reflected properties
cannot carry, so they have verbs of their own:

```js
particles.setColourKeys(fx, [
  {time: 0.0, r: 4.0, g: 1.6, b: 0.4, a: 1.0},   // linear, MAY exceed 1 —
  {time: 0.4, r: 1.6, g: 0.5, b: 0.1, a: 0.9},   // that is what makes fire bloom
  {time: 1.0, r: 0.2, g: 0.2, b: 0.2, a: 0.0}
]);
particles.setScaleKeys(fx, [{time: 0, scale: 0.4}, {time: 1, scale: 1.6}]);
particles.colourKeys(fx);  particles.scaleKeys(fx);
```

- Up to 6 keys each; `time` is a life fraction in 0..1, ascending.
- An empty list clears the ramp.
- A system with a scale ramp draws SQUARE particles — the ramp replaces the
  emitter's dimensions rather than multiplying them.

`particles.describe(fx)` is the resolved authoring state including the ramps —
a read-only overview. For the exact spellings you can WRITE, use
`node.properties(fx)`: describe's key names are a summary and do not all match
`setProperty` keys.

## Timing

`particles.timeScale()` is the scene's particle clock: 1 real time, 0 freezes
every emitter, 2 double speed. It is SCENE-wide (and process-wide in the
renderer) — there is one clock, not one per emitter.

## Seeing it — particles need frames

An emitter's state is whatever the simulation has reached, so a screenshot
taken immediately after creation shows almost nothing. Advance time
DETERMINISTICALLY before looking:

```js
editor.frame(60, 1/60);      // exactly one second of simulation
```

Without the `dt` argument each stepped frame is charged the wall-clock time of
the previous statement — about a millisecond — and you photograph an emitter
that has barely started.

Then aim and shoot: `screenshot({frameNode: {id: fx, pitch: 10, distance: 6}})`.

If nothing appears at all, it is usually a renderer refusal rather than a bad
number: read the `engineErrors` block on the `run_script` response, or
`app.engineErrors()` after a frame (the `jahshaka-scene-building` skill's
Debugging section has the loop).

Every scripted change here is undoable as part of the run's single undo step,
except the texture binding's asset pin.
