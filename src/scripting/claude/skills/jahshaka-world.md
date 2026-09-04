---
name: jahshaka-world
description: Drive Jahshaka's World Modes — the low/medium/high/epic quality tiers, per-row overrides for MSAA, HDR, bloom, ambient occlusion, SMAA, shadows, GI, sky and planar reflections, plus the post-chain tuning. Use when the user asks to make the scene faster or prettier, or asks about quality, anti-aliasing, shadows, GI or post-processing settings.
version: 1
---

# World Modes — the quality tier and its rows

Every scalability setting in a Jahshaka scene resolves through **one World
Mode**: `low`, `medium`, `high`, `epic`, or `custom`. Applying a mode writes
each row's tier value into the scene. Individual rows can be **pinned** on top
of the mode, and a pin survives mode switches until you drop it.

This is the layer above `world.fog` / `world.sky` / `world.shadows` (those are
in the `jahshaka-scene-building` skill) — same scene, different question:
*how expensive should it look?*

## Read before you write

```js
world.mode();       // "high" | "low" | "medium" | "epic" | "custom"
world.settings();   // every row, resolved
```

`world.settings()` returns one entry per row:

```js
{ msaa: { value: 1, valueId: "off", label: "Off",
          source: "mode", tierValue: 1, available: true }, ... }
```

- `source` — `"override"` (the user pinned it), `"mode"` (from the tier) or
  `"custom"` (no tier is applied).
- `valueId` is the spelling `world.override` takes; `value` is the raw number.
- **`available: false` means the renderer does not serve that row yet.** Say so
  instead of setting it and reporting success.

`world.modeTable()` is the registry itself — every row's id, label, group,
options, per-tier values and a cost note explaining what it actually costs.
Read it when the user asks "what would that do to performance?"; it is the same
text the World panel shows.

## Switching tiers

```js
world.mode({ mode: "high" });    // world.setMode is the same verb
```

Rows the user pinned are NOT overwritten by a mode switch — that is the whole
point of a pin. If a tier switch appears to do nothing to a setting, look for
`source: "override"` on that row.

## Pinning one row

```js
world.override({ id: "msaa", value: "4x" });
world.override({ id: "giMode", value: "vct" });
world.clearOverride({ id: "msaa" });   // back to the mode's value
world.clearOverrides();                // drop every pin, re-apply the mode
```

The rows, with their value spellings:

| group | id | values |
|---|---|---|
| Rendering | `msaa` | `off`, `2x`, `4x`, `8x` |
| Rendering | `hdr` | `on` / `off` |
| Rendering | `bloom` | `on` / `off` |
| Rendering | `ssao` | `off`, `half`, `full` |
| Rendering | `smaa` | `on` / `off` |
| Rendering | `ssr`, `refractions` | declared; check `available` |
| Shadows | `shadowResolution` | pixel sizes, or Auto |
| Shadows | `shadowFilter` | `auto`, `hard`, `soft`, `verysoft` |
| Global Illumination | `giMode` | `off`, `instant_radiosity`, `vct`, `vct_pcc_hybrid` |
| Global Illumination | `giQuality` | `low`, `medium`, `high` |
| Sky | `skyBakeResolution` | `256`, `512`, `1024` |
| Sky | `ambientFromSky` | `on` / `off` |
| Reflections | `planarBudget` | how many mirror planes may render |

Values may be given as the id spelling (`"4x"`, `"vct"`, `"off"`) or as the raw
number. Always confirm with the returned row state rather than assuming.

## Two traps worth knowing

1. **MSAA and the post chain do not combine.** With HDR or ambient occlusion
   on, hardware MSAA is ignored — anti-aliasing comes from `smaa` instead.
   Every tier ships `msaa: off` for that reason. Setting `msaa` to `4x` on a
   scene with the post chain on buys nothing; recommend `smaa` instead.
2. **Bloom needs HDR.** It rides the HDR chain and does nothing without it.

## Continuous tuning (not a tier row)

The post chain's on/off switches are World Mode rows; its dials are not:

```js
world.postFx({ exposure: 0.4, bloomThreshold: 1.2,
               ssaoPower: 1.5, ssaoRadius: 0.8 });   // returns the new state
```

Planar reflections have their own read/write pair, where a budget of `-1` (or
`"auto"`) means "follow the World Mode":

```js
world.planarReflections();
world.setPlanarReflections({ budget: 2, resolution: 512, shadows: false });
```

And the two settings that also exist as plain verbs report the ACHIEVED value
when the engine is live, not the requested one:

```js
world.antiAliasing();        // the driver may clamp what was asked for
world.shadowResolution();    // 0 = Auto with no shadow caster yet
```

## Verify visually — the numbers are not the picture

Quality changes are exactly the kind of edit that looks fine in JSON and wrong
on screen. Render, then look:

```js
world.override({ id: "giMode", value: "vct" });
editor.frame(3);                       // GI needs frames to build
console.log(JSON.stringify(app.engineErrors()));
```

Then `screenshot({postFx: true})` (the default — it applies the chain, so the
image matches what the user sees). `postFx: false` renders neutrally, which is
how you tell a post-chain problem from a lighting one.

A row that refuses at the renderer shows up in `engineErrors`, never as a
script error — the `jahshaka-scene-building` skill's Debugging section has the
loop. Heavier tiers also cost SHADER COMPILATION on first use:
`editor.warmUpShaders()` pays it up front instead of on the frames the user
watches.
