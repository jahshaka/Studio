/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/worldmodes.h"

#include "irisgl/document/scenegraph/scene.h"

#include <QJsonValue>

namespace worldmodes {

namespace {

/// The planar-reflection budget per tier, shared by the row below and by the
/// resolver beside it (which cannot call rows() — it runs while rows() is being
/// built). Low, Medium, High, Epic.
const int kPlanarTier[4] = { 0, 0, 1, 2 };

/// The row's `get`. iris::Scene::planarReflectionBudget carries -1 for "never
/// set, follow the mode" — the one negative value the field can hold — and the
/// registry's contract is that a row's get() returns the RESOLVED value. Every
/// path that applies a mode writes a concrete number through, so -1 only ever
/// survives in a Custom-mode scene or one loaded from a document written before
/// the feature existed; both resolve to "off", which is also exactly what
/// SceneMirror does with a negative budget.
int planarBudgetOf(const iris::ScenePtr &s)
{
    if (!s) return 0;
    if (s->planarReflectionBudget >= 0) return s->planarReflectionBudget;
    const int m = s->worldMode;
    return (m >= 0 && m <= 3) ? kPlanarTier[m] : 0;
}

/// The four tier columns, in order: Low, Medium, High, Epic.
/// Values and reasons come from POST_CHAIN_SPEC.md §9.3, with two documented
/// departures: hardware MSAA is 1x in every tier (it cannot be combined with the
/// post chain — see that row), and screen-space reflections are DECLARED but not
/// yet served, the same contract shape planar reflections used before its lane
/// landed. A row that silently does nothing is worse than a row that says so.
///
/// EPIC WAS RETUNED (fps audit F6, perf wave 2026-09-06). Three rows moved and
/// the reason is the same in all three: they were the tier's per-pixel and
/// per-frame heavyweights and none of them was carrying its cost in visible
/// quality — full-res 64-tap SSAO -> half res, a 4096 shadow atlas -> 2048,
/// PCF 6x6 -> 4x4. HDR, bloom, SMAA Ultra, VCT GI and the planar budget are
/// UNTOUCHED: Epic is still the tier that turns everything on. Each row carries
/// its own note; the before/after screenshots that justified it are in the
/// wave's report.
QVector<Row> buildRows()
{
    QVector<Row> out;

    // ---- Rendering ---------------------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("msaa");
        r.label = QStringLiteral("Anti-Aliasing (MSAA)");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"), QStringLiteral("Off"), 1 },
                      { QStringLiteral("2x"),  QStringLiteral("2x"),  2 },
                      { QStringLiteral("4x"),  QStringLiteral("4x"),  4 },
                      { QStringLiteral("8x"),  QStringLiteral("8x"),  8 } };
        // 1x in every tier, and that is a FORCED choice, not a taste one: with
        // the post chain on, hardware MSAA either crashes the driver (HDR) or
        // renders black (ambient occlusion) — both reproduced in tests/engine.
        // The chain renders at 1x regardless of what is asked here, so a tier
        // that asked for 4x would be paying for a multisampled window that does
        // nothing. Anti-aliasing comes from SMAA instead; MSAA stays available
        // as a row for scenes that run with the chain off.
        r.tier[0] = 1; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Hardware edge smoothing. Costs render-target memory and "
                                "bandwidth in proportion to the sample count, and the driver may "
                                "clamp the request. IGNORED while HDR or ambient occlusion is on "
                                "— those use SMAA instead.");
        r.get = [](const iris::ScenePtr &s) { return s->antiAliasing; };
        r.set = [](const iris::ScenePtr &s, int v) { s->antiAliasing = v; };
        out.append(r);
    }

    // ---- Post-processing chain (POST_CHAIN_SPEC.md phases 3-7) --------------
    // These rows appear only once the renderer can actually serve them: a row
    // that silently does nothing is worse than no row at all.
    {
        Row r;
        r.id = QStringLiteral("hdr");
        r.label = QStringLiteral("HDR + Filmic Tonemap");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Bool;
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Renders into a floating-point buffer and grades it with a film "
                                "curve and automatic exposure. The luminance and bloom work is at "
                                "a fixed size, so only the final pass scales with the window. Off "
                                "at Low purely to save the buffer.");
        r.get = [](const iris::ScenePtr &s) { return s->hdrEnabled ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { s->hdrEnabled = v != 0; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("bloom");
        r.label = QStringLiteral("Bloom");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Bool;
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Highlight glow. Rides the HDR chain's fixed 256x256 blur, so it "
                                "costs almost nothing and does not scale with resolution — but it "
                                "needs HDR, and does nothing without it.");
        r.get = [](const iris::ScenePtr &s) { return s->bloomEnabled ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { s->bloomEnabled = v != 0; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("ssao");
        r.label = QStringLiteral("Ambient Occlusion");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),  QStringLiteral("Off"),        0 },
                      { QStringLiteral("half"), QStringLiteral("Half Res"),   1 },
                      { QStringLiteral("full"), QStringLiteral("Full Res"),   2 } };
        // EPIC IS HALF-RES, NOT FULL (fps audit F6). The shader takes 64 samples
        // per pixel and that count is not adjustable, so the buffer resolution
        // is the only lever there is — and at 3440x1440 full-res that is 64
        // taps across 4.95 Mpx, the single most expensive per-pixel item in
        // the tier. Ogre's own SSAO sample runs half-res; the AO signal is
        // low-frequency by nature (it is upsampled and multiplied into ambient,
        // not sampled as detail), so the visible difference is small and the
        // frame-time difference is not.
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Contact shadowing in creases and corners. 64 samples per pixel, "
                                "fixed by the shader — the only lever is the buffer resolution. "
                                "Full Res is four times the samples of Half Res for a "
                                "low-frequency signal; Half Res is what the tiers use. "
                                "Also adds a second colour attachment to the MAIN pass, which is "
                                "why it is off below High.");
        // One row, two backing fields: the enable flag and the buffer scale.
        r.get = [](const iris::ScenePtr &s) {
            if (!s->ssaoEnabled) return 0;
            return s->ssaoScale >= 0.99f ? 2 : 1;
        };
        r.set = [](const iris::ScenePtr &s, int v) {
            s->ssaoEnabled = v != 0;
            if (v) s->ssaoScale = v >= 2 ? 1.0f : 0.5f;
        };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("smaa");
        r.label = QStringLiteral("SMAA (post AA)");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),    QStringLiteral("Off"),    -1 },
                      { QStringLiteral("low"),    QStringLiteral("Low"),     0 },
                      { QStringLiteral("medium"), QStringLiteral("Medium"),  1 },
                      { QStringLiteral("high"),   QStringLiteral("High"),    2 },
                      { QStringLiteral("ultra"),  QStringLiteral("Ultra"),   3 } };
        // SMAA is the chain's anti-aliasing at every tier that has a chain.
        // POST_CHAIN_SPEC §9.3 proposed MSAA at High/Epic and SMAA only at
        // Medium; that split is not available (see the MSAA row above), so the
        // preset simply climbs with the tier instead. Low has no chain and no AA.
        r.tier[0] = -1; r.tier[1] = 1; r.tier[2] = 2; r.tier[3] = 3;
        r.cost = QStringLiteral("Edge anti-aliasing after tonemapping, at a fixed cost per pixel "
                                "instead of MSAA's per-sample memory. Changing the preset "
                                "recompiles three shaders — a brief hitch, not a per-frame cost.");
        r.get = [](const iris::ScenePtr &s) { return s->smaaPreset; };
        r.set = [](const iris::ScenePtr &s, int v) { s->smaaPreset = v; };
        out.append(r);
    }
    {
        // SSR SHIPPED (POST_CHAIN_SPEC §8 phase 6; OgreChain.cpp's SSR block and
        // irisgl/engine/media/Hlms/Jahshaka/JahSsr*). The row is served: a
        // depth/normal/roughness prepass feeds a ray march that HlmsPbs composites
        // into the specular environment term itself (`hlms_use_ssr`), so where the
        // march is confident the screen replaces the probe/sky answer and where it
        // is not the pixel is byte-for-byte what it is today. Pixel gate:
        // `ssr.engine`.
        //
        // THE TIERS ARE THE MEASUREMENT, not a guess. On the RTX 4080 SUPER
        // baseline, offscreen 1x, a 62-object fixture (tests/ssr with
        // JAH_SSR_BENCH=1):
        //     1920x1080   passthrough 0.41 ms   half-res +0.07..0.10   full-res +0.11..0.13
        //     3840x2160   passthrough 0.44 ms   half-res +0.11         full-res +0.54
        //     1920x1080, 602 objects              half-res +0.13        full-res +0.15
        // Two things follow. (a) The ray march is pixel-bound and half resolution
        // really is a quarter of it — at 4K the two rows separate by 5x, which is
        // why the quality row exists at all. (b) At 1080p the march is BELOW the
        // measurement floor and what is actually being paid is the second scene
        // traversal, which grows with object count, not with resolution. That is
        // the reason this row stays off below High: the cost a heavy world pays is
        // CPU submission, and this renderer is CPU-bound long before it is
        // pixel-bound.
        Row r;
        r.id = QStringLiteral("ssr");
        r.label = QStringLiteral("Screen-Space Reflections");
        r.group = QStringLiteral("Reflections");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),  QStringLiteral("Off"),           0 },
                      { QStringLiteral("half"), QStringLiteral("Half-Res Rays"), 1 },
                      { QStringLiteral("hq"),   QStringLiteral("Full-Res Rays"), 2 } };
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 1; r.tier[3] = 2;
        r.cost = QStringLiteral("Reflections of things that MOVE — the one gap a baked probe "
                                "structurally cannot fill, and the only reflection source that "
                                "needs no capture at all. Costs a second traversal of the scene "
                                "(a depth/normal/roughness prepass) on top of the ray march, so "
                                "it never appears below High. Only reflects what is ON SCREEN: "
                                "reflections fade out at the frame's edges and on rough surfaces, "
                                "and fall back to the sky and probes wherever they do.");
        r.available = true;
        r.get = [](const iris::ScenePtr &s) { return s->ssrMode; };
        r.set = [](const iris::ScenePtr &s, int v) { s->ssrMode = v; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("refractions");
        r.label = QStringLiteral("Refractive Glass");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),  QStringLiteral("Off"),  0 },
                      { QStringLiteral("auto"), QStringLiteral("Auto"), 1 },
                      { QStringLiteral("on"),   QStringLiteral("On"),   2 } };
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Glass that BENDS what is behind it. Auto is the honest setting: "
                                "the extra scene pass and full-screen copy only enter the frame "
                                "while the scene actually contains a refractive material, so it "
                                "costs nothing until you make one.");
        r.get = [](const iris::ScenePtr &s) { return s->refractionsMode; };
        r.set = [](const iris::ScenePtr &s, int v) { s->refractionsMode = v; };
        out.append(r);
    }
    {
        // DISTORTION (POST_LOOKS_SPEC.md §5.3). MACHINERY, so it IS a tiered row
        // — unlike the looks stack, which is an art choice and deliberately
        // tierless (§7 R10): a mode switch must never silently drop somebody's
        // look, but it may reasonably drop a heat-haze pass at Low.
        Row r;
        r.id = QStringLiteral("distortion");
        r.label = QStringLiteral("Distortion");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),  QStringLiteral("Off"),  0 },
                      { QStringLiteral("auto"), QStringLiteral("Auto"), 1 },
                      { QStringLiteral("on"),   QStringLiteral("On"),   2 } };
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Objects that WARP what is behind them — heat haze, blast "
                                "waves, shock rings. Give a material the Distortion shading "
                                "model and its Normal Map becomes the displacement. Auto is "
                                "the honest setting: the extra target and its two passes only "
                                "enter the frame while the scene actually holds such a "
                                "material, so it costs nothing until you make one.");
        r.get = [](const iris::ScenePtr &s) { return s->distortionMode; };
        r.set = [](const iris::ScenePtr &s, int v) { s->distortionMode = v; };
        out.append(r);
    }

    // ---- Shadows -----------------------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("shadowResolution");
        r.label = QStringLiteral("Shadow Map Size");
        r.group = QStringLiteral("Shadows");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("auto"), QStringLiteral("Auto"), 0 },
                      { QStringLiteral("512"),  QStringLiteral("512"),  512 },
                      { QStringLiteral("1024"), QStringLiteral("1024"), 1024 },
                      { QStringLiteral("2048"), QStringLiteral("2048"), 2048 },
                      { QStringLiteral("4096"), QStringLiteral("4096"), 4096 } };
        // EPIC IS 2048, NOT 4096 (fps audit F6). The atlas is R x 3.5R at 32-bit
        // depth, so 4096 is ~235 MB of VRAM *and* four times the shadow-pass
        // fill of 2048 — the whole caster set is rasterised into it every
        // frame. 2048 is what High already used and what the editor's typical
        // scene scale actually resolves; the row is still there for anyone who
        // wants to pin 4096 on a static shot.
        r.tier[0] = 512; r.tier[1] = 1024; r.tier[2] = 2048; r.tier[3] = 2048;
        r.cost = QStringLiteral("One shadow atlas for the whole scene, R wide by 3.5R tall at "
                                "32-bit depth: 512 costs ~3.6 MB, 1024 ~14 MB, 2048 ~59 MB, "
                                "4096 ~235 MB of VRAM — and four times 2048's rasterisation "
                                "work every frame, which is why no tier asks for it.");
        r.get = [](const iris::ScenePtr &s) { return s->shadowResolution; };
        r.set = [](const iris::ScenePtr &s, int v) { s->shadowResolution = v; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("shadowMapBudget");
        r.label = QStringLiteral("Shadow Map Budget");
        r.group = QStringLiteral("Shadows");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("auto"), QStringLiteral("Auto"), 0 },
                      { QStringLiteral("2"),    QStringLiteral("2 lights"),  2 },
                      { QStringLiteral("4"),    QStringLiteral("4 lights"),  4 },
                      { QStringLiteral("8"),    QStringLiteral("8 lights"),  8 },
                      { QStringLiteral("16"),   QStringLiteral("16 lights"), 16 } };
        // THE DEFECT THIS ROW EXISTS FOR (SHADOW_TOOLING_SPEC.md §1): the atlas
        // used to hold exactly TWO point/spot shadow maps, forever, and Ogre
        // fills them with the casters closest to the camera and drops the rest
        // without a word. The shipped Showroom has three shadow-casting lamps;
        // one of them has never cast a shadow, and which one changed as the
        // camera moved.
        //
        // TIERS 2/4/8/8 (owner decision D1). Low keeps today's two; Medium's
        // four covers the ordinary "a lamp in each corner" room; High and Epic
        // sit at eight, which is where the shadow-pass cost — six cube faces
        // plus a copy for EVERY mapped point light — starts to be the thing
        // you are paying for rather than the atlas.
        r.tier[0] = 2; r.tier[1] = 4; r.tier[2] = 8; r.tier[3] = 8;
        r.cost = QStringLiteral("A ceiling, not an allocation: the engine grows the atlas in "
                                "steps (2, 4, 8, 16) to fit the scene's shadow-casting point and "
                                "spot lights, and never shrinks it again in one session. Each "
                                "focused map costs R x R of the atlas (at 2048: ~17 MB) and, for "
                                "a POINT light, six cube-face renders plus a copy every frame. "
                                "Empty maps cost no shaders. Lights beyond the budget still light "
                                "the scene; they simply cast no shadow.");
        r.get = [](const iris::ScenePtr &s) { return s->shadowMapBudget; };
        r.set = [](const iris::ScenePtr &s, int v) { s->shadowMapBudget = v; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("shadowFilter");
        r.label = QStringLiteral("Shadow Softness");
        r.group = QStringLiteral("Shadows");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("auto"),     QStringLiteral("Auto"),      -1 },
                      { QStringLiteral("hard"),     QStringLiteral("Hard"),       0 },
                      { QStringLiteral("soft"),     QStringLiteral("Soft"),       1 },
                      { QStringLiteral("verysoft"), QStringLiteral("Very Soft"),  2 } };
        // EPIC IS SOFT (4x4), NOT VERY SOFT (6x6) — fps audit F6. 6x6 is 36
        // shadow-map taps per shaded pixel against 4x4's 16, more than double,
        // for a softening step most scenes cannot be shown to need at 2048.
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("PCF filter width for every shadowed light: Hard 2x2, Soft 4x4, "
                                "Very Soft 6x6 taps — 36 taps per shaded pixel against 16, which "
                                "is why no tier asks for Very Soft. Auto uses the softest quality "
                                "any light in the scene asked for. Takes effect next frame; no "
                                "rebuild.");
        r.get = [](const iris::ScenePtr &s) { return s->shadowFilterTier; };
        r.set = [](const iris::ScenePtr &s, int v) { s->shadowFilterTier = v; };
        out.append(r);
    }

    // ---- Global illumination = RAYON (GI_UNIFIED_SPEC.md §2) ---------------
    // ONE row is the dial the World Mode drives; the five below it are the
    // machinery that dial consumes, and they are `rayonTiered` — resolved by
    // the RAYON tier, not by the World Mode, because two dials must never own
    // one backing field. THE TABLE ITSELF is kRayonTable further down; the
    // tier[] columns here must agree with it (gi.tiers pins both).
    {
        Row r;
        r.id = QStringLiteral("rayon");
        r.label = QStringLiteral("Rayon (realtime GI)");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),    QStringLiteral("Off"),    0 },
                      { QStringLiteral("low"),    QStringLiteral("Low"),    1 },
                      { QStringLiteral("medium"), QStringLiteral("Medium"), 2 },
                      { QStringLiteral("high"),   QStringLiteral("High"),   3 },
                      { QStringLiteral("epic"),   QStringLiteral("Epic"),   4 } };
        // THE WORLD MODE'S OPINION ABOUT RAYON, and it is the OLD table read
        // through the new dial — deliberately, so that switching a scene's
        // World Mode keeps doing what it did:
        //   Low/Medium had GI off             -> Rayon off
        //   High had Instant Radiosity + low  -> Rayon Low (the same two fields)
        //   Epic had the hybrid               -> Rayon Epic
        // Epic is the one column that MOVES, and moving it is the point of this
        // phase (owner decision D2: a new scene is born Realtime-Epic, which is
        // hybrid + high + DDGI where it used to be hybrid + medium).
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 1; r.tier[3] = 4;
        r.cost = QStringLiteral("Rayon — realtime global illumination: light that bounces off "
                                "surfaces and colours everything it lands on, recomputed live "
                                "instead of baked. The quality tier picks the techniques for you: "
                                "Low bounces one light off the scene (cheapest, no voxels); Medium "
                                "voxelizes the lit volume and feeds an irradiance field from it — "
                                "probe-stored bounce that cannot leak through walls; High adds a "
                                "grid of reflection probes with HDR, shadowed captures; Epic adds "
                                "three light bounces and dynamic reflection probes that follow "
                                "whatever moves, frame by frame. Every knob a tier sets is still "
                                "reachable one by one under Advanced, and anything you set there "
                                "stays set.");
        r.get = [](const iris::ScenePtr &s) {
            if (!s || s->giMode == iris::GiMode::OFF) return 0;
            return qBound(0, s->giTier, 3) + 1;
        };
        r.set = [](const iris::ScenePtr &s, int v) {
            if (!s) return;
            const bool on = v > 0;
            const RayonTier t = on ? RayonTier(qBound(0, v - 1, 3)) : rayonTier(s);
            setRayon(s, on, t);
        };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giMode");
        r.label = QStringLiteral("Rayon Technique");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.rayonTiered = true;
        r.options = { { QStringLiteral("off"),              QStringLiteral("Off"),               0 },
                      { QStringLiteral("instant_radiosity"), QStringLiteral("Instant Radiosity"), 1 },
                      { QStringLiteral("vct"),              QStringLiteral("VCT"),               2 },
                      { QStringLiteral("vct_pcc_hybrid"),   QStringLiteral("VCT + Probes"),      3 } };
        // RAYON columns (Low, Medium, High, Epic) — not world-mode ones.
        // Low keeps Instant Radiosity (spec §9 D1: "low is really low"); the
        // top two tiers are the hybrid and differ in the giBounces and
        // giDynamicProbes rows below, not here.
        r.tier[0] = 1; r.tier[1] = 2; r.tier[2] = 3; r.tier[3] = 3;
        r.cost = QStringLiteral("Which technique Rayon uses. Instant Radiosity re-traces on light "
                                "moves and sees only the driving light; VCT re-voxelizes on "
                                "geometry edits (editing latency, not frame time) and lights from "
                                "everything; VCT + Probes adds sharp reflections near geometry — "
                                "six renders per reflection probe on every re-solve (18 probes by "
                                "default), HDR and shadowed at High quality. Normally the Rayon "
                                "quality tier picks this; setting it here PINS it.");
        r.get = [](const iris::ScenePtr &s) { return int(s->giMode); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giMode = iris::GiMode(v); };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giQuality");
        r.label = QStringLiteral("Rayon Voxel/Probe Quality");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.rayonTiered = true;
        r.options = { { QStringLiteral("low"),    QStringLiteral("Low"),    0 },
                      { QStringLiteral("medium"), QStringLiteral("Medium"), 1 },
                      { QStringLiteral("high"),   QStringLiteral("High"),   2 } };
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 2; r.tier[3] = 2;
        r.cost = QStringLiteral("Ray/voxel budget: 32/64/128 voxels per axis and 128/256/512 pixel "
                                "probe faces. In VCT + Probes it ALSO turns on HDR and shadowed "
                                "probe captures at High (world.gi's probeHdr/probeShadows pin "
                                "either one independently). High is a re-solve-latency trap in an "
                                "editor: every geometry or light edit pays for it again.");
        r.get = [](const iris::ScenePtr &s) { return int(s->giQuality); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giQuality = iris::GiQuality(v); };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giDdgi");
        r.label = QStringLiteral("Rayon Irradiance Field (DDGI)");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.rayonTiered = true;
        r.options = { { QStringLiteral("off"), QStringLiteral("Off"), 0 },
                      { QStringLiteral("on"),  QStringLiteral("On"),  1 } };
        // Owner option (b), 2026-09-09: every voxel tier feeds the field — it
        // is the one diffuse arm that is right in both open and sealed scenes
        // (rayon2 S1-S3). Low has no voxel volume to feed it from.
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("The irradiance field: a grid of probes over the lit volume storing "
                                "the bounced light arriving from every direction, plus a depth map "
                                "that decides what each probe can actually see. It is the LEAK FIX "
                                "— cone-traced bounce blows out corners because a cone cannot tell "
                                "a wall from empty space. Turning it on turns the voxel-cone "
                                "diffuse OFF (it replaces that term rather than adding to it); "
                                "reflections, probes and planar are untouched. Needs a voxel "
                                "technique (VCT or VCT + Probes) to be fed from, which is why "
                                "every tier from Medium up turns it on and Low cannot.");
        // -1 (auto) is what a scene no tier has ever been applied to holds, and
        // the engine renders it as OFF — so that is what it RESOLVES to. Any
        // tier application writes a concrete 0/1 through.
        r.get = [](const iris::ScenePtr &s) { return s->giDdgi > 0 ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { s->giDdgi = v ? 1 : 0; };
        out.append(r);
    }
    // EPIC'S TWO COLUMNS (owner option (b), 2026-09-09): what makes the top
    // tier a tier of its own now that High is DDGI-fed too. Both are ordinary
    // Int rows over document fields the engine already consumes; the engine's
    // quality dial is untouched (it is the RESOLUTION dial, and Epic changes
    // no resolution).
    {
        Row r;
        r.id = QStringLiteral("giBounces");
        r.label = QStringLiteral("Rayon Light Bounces");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Int;
        r.rayonTiered = true;
        r.minValue = 1; r.maxValue = 4;
        r.tier[0] = 1; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 3;
        r.cost = QStringLiteral("Total light bounces, 1-4. Each bounce past the first is another "
                                "light-propagation pass over the whole voxel volume on every "
                                "re-solve (128^3 at High/Epic), and the irradiance field is fed "
                                "from that volume, so the extra bounces reach the probe-stored "
                                "diffuse too. Epic's column: 3; every other tier 1. Under "
                                "Instant Radiosity it is the ray bounce count instead.");
        r.get = [](const iris::ScenePtr &s) { return qBound(1, s->giNumBounces, 4); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giNumBounces = qBound(1, v, 4); };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giDynamicProbes");
        r.label = QStringLiteral("Rayon Dynamic Probes");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Int;
        r.rayonTiered = true;
        r.minValue = 0; r.maxValue = 8;
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 0; r.tier[3] = 2;
        r.cost = QStringLiteral("Extra reflection-probe re-captures per frame, on top of the GI "
                                "Update Budget, reserved for the probes covering whatever MOVED "
                                "this frame — so a moving object's reflection follows it frame by "
                                "frame instead of waiting its turn in the budget's sweep. Costs "
                                "nothing while the scene is still; while something moves, up to "
                                "this many probe captures a frame (~2 ms each in Debug). Epic's "
                                "column: 2; every other tier 0 (the sweep alone). VCT + Probes "
                                "only.");
        r.get = [](const iris::ScenePtr &s) { return qBound(0, s->giDynamicProbes, 8); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giDynamicProbes = qBound(0, v, 8); };
        out.append(r);
    }

    // ---- Sky ---------------------------------------------------------------
    {
        Row r;
        r.id = QStringLiteral("skyBakeResolution");
        r.label = QStringLiteral("Sky Detail");
        r.group = QStringLiteral("Sky");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("256"),  QStringLiteral("256"),  256 },
                      { QStringLiteral("512"),  QStringLiteral("512"),  512 },
                      { QStringLiteral("1024"), QStringLiteral("1024"), 1024 } };
        r.tier[0] = 256; r.tier[1] = 256; r.tier[2] = 512; r.tier[3] = 1024;
        r.cost = QStringLiteral("Equirect width the analytic sky is CPU-baked at. Costs bake time "
                                "when the sky changes, never frame time.");
        r.get = [](const iris::ScenePtr &s) { return s->skyBakeResolution; };
        r.set = [](const iris::ScenePtr &s, int v) { s->skyBakeResolution = v; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("ambientFromSky");
        r.label = QStringLiteral("Sky Ambient");
        r.group = QStringLiteral("Sky");
        r.type = RowType::Bool;
        r.tier[0] = 1; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("Ambient light integrated from the live sky instead of the flat "
                                "Ambient Color. One CPU integral per sky change — cheap in every "
                                "tier; it is a row for discoverability, not for performance.");
        r.get = [](const iris::ScenePtr &s) { return s->ambientFromSky ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { s->ambientFromSky = v != 0; };
        out.append(r);
    }

    // ---- Reflections -------------------------------------------------------
    // This row was declared here with `available = false` before the engine
    // could serve it; the planar lane filled in get/set and flipped the flag,
    // and not one consumer of this file changed. That is what the contract-row
    // idea was for.
    //
    // Epic is 2, not the 4 this row was drafted with. Each active plane is a
    // FULL extra scene render inside the editor's per-frame loop, which shares
    // the machine with Qt, the scripting host and the MCP server: four planes
    // means five scene renders and an editor that stops feeling interactive on
    // any scene heavier than a sample. Two mirrors facing the camera at once is
    // already an unusual composition, so the visible payoff of 3 and 4 is small.
    // The verb and this row still ACCEPT up to 8 for anyone who wants it, and
    // an artist can force which mirror wins; only the preset is conservative.
    {
        Row r;
        r.id = QStringLiteral("planarBudget");
        r.label = QStringLiteral("Planar Reflections");
        r.group = QStringLiteral("Reflections");
        r.type = RowType::Int;
        r.minValue = 0; r.maxValue = 8;
        r.tier[0] = kPlanarTier[0]; r.tier[1] = kPlanarTier[1];
        r.tier[2] = kPlanarTier[2]; r.tier[3] = kPlanarTier[3];
        r.cost = QStringLiteral("How many mirror planes may re-render the scene. One extra full "
                                "scene render per plane — the most expensive row in the table. "
                                "CHANGING IT REBUILDS SHADERS: the count is baked into the PBR "
                                "shader, not passed as a uniform, so expect a pause on the next "
                                "frame. Planes are placed per object (Properties > Planar "
                                "Reflector); this is only the cap on how many of them render.");
        r.get = [](const iris::ScenePtr &s) { return planarBudgetOf(s); };
        r.set = [](const iris::ScenePtr &s, int v) { s->planarReflectionBudget = v; };
        out.append(r);
    }

    return out;
}

/// The override map is JSON on the document, so values round-trip as doubles.
bool overrideValue(const iris::ScenePtr &scene, const QString &id, int &out)
{
    if (!scene) return false;
    const QJsonValue v = scene->worldOverrides.value(id);
    if (v.isUndefined() || v.isNull()) return false;
    out = v.toInt();
    return true;
}

}   // namespace

const QVector<Row> &rows()
{
    static const QVector<Row> table = buildRows();
    return table;
}

const Row *row(const QString &id)
{
    for (const Row &r : rows())
        if (r.id == id) return &r;
    return nullptr;
}

// ---------------------------------------------------------------------------
// THE CONTINUOUS POST-PROCESS PARAMETERS (fix wave 2026-09-07 item 8).
//
// Not tiered, on purpose (see ParamRow's note and world.postFx's): a mode
// switch answers "how much machinery", never "how should this look". What they
// ARE is declared once, here, so the World > Post Process section and the
// world.postFx verb cannot disagree about a range, a label or what a number
// means — which is the exact failure this file was written to prevent for the
// on/off rows and which the parameters had, in miniature, all along (the
// ranges lived inside the verb and nothing else could see them).

static QVector<ParamRow> buildPostFxParams()
{
    QVector<ParamRow> out;
    {
        ParamRow p;
        p.id = QStringLiteral("exposure");
        p.label = QStringLiteral("Exposure");
        p.ownerRowId = QStringLiteral("hdr");
        p.minValue = -8.0; p.maxValue = 8.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("The auto-exposure midpoint. NOT stops: the value is used as "
                               "e^(exposure-2), so +0.69 is one doubling. The scene default is "
                               "+0.6, which is what puts mid-grey back where it was when HDR "
                               "comes on.");
        p.get = [](const iris::ScenePtr &s) { return double(s->exposure); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposure = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("exposureMin");
        p.label = QStringLiteral("Exposure Min");
        p.ownerRowId = QStringLiteral("hdr");
        p.minValue = -8.0; p.maxValue = 8.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("The bottom of the window automatic exposure may adapt within. "
                               "Set equal to Exposure Max to PIN the exposure — the "
                               "deterministic setting, and the one every secondary surface "
                               "(thumbnails, previews, screenshots) grades with.");
        p.get = [](const iris::ScenePtr &s) { return double(s->exposureMin); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposureMin = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("exposureMax");
        p.label = QStringLiteral("Exposure Max");
        p.ownerRowId = QStringLiteral("hdr");
        p.minValue = -8.0; p.maxValue = 8.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("The top of the auto-exposure window. A narrow window is a "
                               "steadier image; a wide one copes with walking from a dark room "
                               "into daylight.");
        p.get = [](const iris::ScenePtr &s) { return double(s->exposureMax); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposureMax = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("bloomThreshold");
        p.label = QStringLiteral("Bloom Threshold");
        p.ownerRowId = QStringLiteral("bloom");
        p.minValue = 0.0; p.maxValue = 64.0; p.perPixelStep = 0.05; p.decimals = 2;
        p.doc = QStringLiteral("Where the bright pass starts, in the tonemapper's units. High "
                               "values read as highlight bloom; low values as a haze filter "
                               "over the whole image.");
        p.get = [](const iris::ScenePtr &s) { return double(s->bloomThreshold); };
        p.set = [](const iris::ScenePtr &s, double v) { s->bloomThreshold = float(v); };
        out.append(p);
    }
    {
        // ADDENDUM A-6. The renderer's bright pass has always taken TWO
        // thresholds (min, full) and the engine hard-coded full = min + 2.
        // Exposed as a WIDTH rather than a second absolute value: a width
        // cannot invert, so the renderer's own `full <= min` clamp becomes
        // unreachable instead of being a state the panel can ask for and the
        // renderer silently refuses. Default 2.0 = byte-identical to before.
        ParamRow p;
        p.id = QStringLiteral("bloomKnee");
        p.label = QStringLiteral("Bloom Knee");
        p.ownerRowId = QStringLiteral("bloom");
        p.minValue = 0.01; p.maxValue = 32.0; p.perPixelStep = 0.05; p.decimals = 2;
        p.doc = QStringLiteral("How wide the ramp above the threshold is. A narrow knee is a "
                               "hard cut — only the brightest pixels bloom; a wide one fades "
                               "the effect in across a range of brightnesses.");
        p.get = [](const iris::ScenePtr &s) { return double(s->bloomKnee); };
        p.set = [](const iris::ScenePtr &s, double v) { s->bloomKnee = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("ssaoPower");
        p.label = QStringLiteral("AO Power");
        p.ownerRowId = QStringLiteral("ssao");
        p.minValue = 0.1; p.maxValue = 8.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("Contrast of the occlusion term — how dark a crease gets.");
        p.get = [](const iris::ScenePtr &s) { return double(s->ssaoPower); };
        p.set = [](const iris::ScenePtr &s, double v) { s->ssaoPower = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("ssaoRadius");
        p.label = QStringLiteral("AO Radius");
        p.ownerRowId = QStringLiteral("ssao");
        p.minValue = 0.05; p.maxValue = 64.0; p.perPixelStep = 0.02; p.decimals = 2;
        p.doc = QStringLiteral("How far the occlusion looks, in metres. Too large and every "
                               "surface shadows every other; too small and only the tightest "
                               "corners darken.");
        p.get = [](const iris::ScenePtr &s) { return double(s->ssaoRadius); };
        p.set = [](const iris::ScenePtr &s, double v) { s->ssaoRadius = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("distortionStrength");
        p.label = QStringLiteral("Distortion Strength");
        p.ownerRowId = QStringLiteral("distortion");
        p.minValue = 0.0; p.maxValue = 8.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("A global multiplier on every distortion material's own "
                               "strength — one dial for how much the whole scene shimmers. "
                               "0 is inert: the frame is bit for bit the frame with no "
                               "distortion at all.");
        p.get = [](const iris::ScenePtr &s) { return double(s->distortionStrength); };
        p.set = [](const iris::ScenePtr &s, double v) { s->distortionStrength = float(v); };
        out.append(p);
    }
    return out;
}

const QVector<ParamRow> &postFxParams()
{
    static const QVector<ParamRow> table = buildPostFxParams();
    return table;
}

const ParamRow *postFxParam(const QString &id)
{
    for (const ParamRow &p : postFxParams())
        if (p.id == id) return &p;
    return nullptr;
}

const QStringList &postFxRowIds()
{
    // ORDER IS THE PANEL'S ORDER, and it is the frame's order: the scene target
    // and its grade first, then what rides on it, then what runs after the
    // tonemap, then the two that are neither (reflections, refraction).
    static const QStringList ids = {
        QStringLiteral("hdr"), QStringLiteral("bloom"), QStringLiteral("ssao"),
        QStringLiteral("smaa"), QStringLiteral("ssr"), QStringLiteral("refractions"),
        QStringLiteral("distortion"),
    };
    return ids;
}

// ---------------------------------------------------------------------------
// RAYON (GI_UNIFIED_SPEC.md §2 / P2) — the unified realtime-GI dial.
//
// Everything below manipulates the three backing fields DIRECTLY rather than
// going through setRowValue(), for two reasons: applying a tier must not record
// pins (setMode's rule, and the bug the panel's `*` marker made visible), and
// the `rayon` row's own set() runs from inside the registry — reaching back
// into rows() from there is a re-entrancy nobody should have to reason about.
// The tier table lives here, once, in a form both this file and the migration
// read.

namespace {

/// THE RAYON TIER TABLE (worldmodes.h's comment is the readable form). Per
/// tier: technique (GiMode ordinal), quality (GiQuality ordinal), ddgi (0/1),
/// bounces (total, 1..4), dynamicProbes (0..8). Owner option (b), 2026-09-09.
struct RayonRow { int technique, quality, ddgi, bounces, dynamicProbes; };
const RayonRow kRayonTable[4] = {
    /* Low    */ { 1, 0, 0, 1, 0 },   // Instant Radiosity, low; nothing to feed a field from
    /* Medium */ { 2, 1, 1, 1, 0 },   // VCT 64^3, DDGI-fed (voxel source)
    /* High   */ { 3, 2, 1, 1, 0 },   // VCT + probes 128^3, HDR + shadowed captures, DDGI-fed
    /* Epic   */ { 3, 2, 1, 3, 2 },   // ... plus 3 bounces and 2 dynamic probes a frame
};
/// The five rayonTiered row ids, in kRayonTable column order.
const int kRayonRowCount = 5;
int rayonColumn(const RayonRow &r, int i) {
    switch (i) {
    case 0: return r.technique;
    case 1: return r.quality;
    case 2: return r.ddgi;
    case 3: return r.bounces;
    default: return r.dynamicProbes;
    }
}

int tierIndex(RayonTier t) { return qBound(0, int(t), 3); }

bool pinned(const iris::ScenePtr &s, const char *id)
{
    return s && s->worldOverrides.contains(QLatin1String(id));
}

}   // namespace

QString rayonRowId() { return QStringLiteral("rayon"); }

QStringList rayonRowIds()
{
    return { QStringLiteral("giMode"), QStringLiteral("giQuality"),
             QStringLiteral("giDdgi"), QStringLiteral("giBounces"),
             QStringLiteral("giDynamicProbes") };
}

QString rayonTierName(RayonTier t)
{
    static const char *names[4] = { "low", "medium", "high", "epic" };
    return QString::fromLatin1(names[tierIndex(t)]);
}

RayonTier rayonTierFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    if (ok) *ok = true;
    if (n == QLatin1String("low"))    return RayonTier::Low;
    if (n == QLatin1String("medium")) return RayonTier::Medium;
    if (n == QLatin1String("high"))   return RayonTier::High;
    if (n == QLatin1String("epic"))   return RayonTier::Epic;
    if (ok) *ok = false;
    return RayonTier::Epic;
}

QStringList rayonTierNames()
{
    return { QStringLiteral("low"), QStringLiteral("medium"),
             QStringLiteral("high"), QStringLiteral("epic") };
}

RayonTier rayonTier(const iris::ScenePtr &scene)
{
    if (!scene) return RayonTier::Epic;
    return RayonTier(qBound(0, scene->giTier, 3));
}

bool rayonEnabled(const iris::ScenePtr &scene)
{
    return scene && scene->giMode != iris::GiMode::OFF;
}

int rayonTechnique(RayonTier t) { return kRayonTable[tierIndex(t)].technique; }
int rayonQuality(RayonTier t)   { return kRayonTable[tierIndex(t)].quality; }
int rayonDdgi(RayonTier t)      { return kRayonTable[tierIndex(t)].ddgi; }
int rayonBounces(RayonTier t)   { return kRayonTable[tierIndex(t)].bounces; }
int rayonDynamicProbes(RayonTier t) { return kRayonTable[tierIndex(t)].dynamicProbes; }

namespace {
/// The five values a scene RENDERS, in kRayonTable column order.
void rayonHave(const iris::ScenePtr &s, int have[5])
{
    have[0] = int(s->giMode);
    have[1] = int(s->giQuality);
    have[2] = s->giDdgi > 0 ? 1 : 0;
    have[3] = qBound(1, s->giNumBounces, 4);
    have[4] = qBound(0, s->giDynamicProbes, 8);
}
}   // namespace

void setRayon(const iris::ScenePtr &scene, bool enabled, RayonTier tier)
{
    if (!scene) return;
    const RayonRow &row = kRayonTable[tierIndex(tier)];
    scene->giTier = tierIndex(tier);

    if (!enabled) {
        // OFF writes exactly ONE field. The technique field IS the on/off
        // switch (there is no second flag to disagree with the renderer), so a
        // pinned technique cannot survive an off — "off, but pinned to VCT" is
        // a state nothing can render, and the pin would silently un-switch
        // Rayon at the next tier application. The machinery rows are left
        // alone: writing a tier's quality into a scene that renders no GI would
        // be churn in the document and in every diff of it, and re-enabling
        // applies the tier anyway.
        scene->worldOverrides.remove(QStringLiteral("giMode"));
        scene->giMode = iris::GiMode::OFF;
        return;
    }
    // The machinery: written unless the user pinned it.
    if (!pinned(scene, "giQuality")) scene->giQuality = iris::GiQuality(qBound(0, row.quality, 2));
    if (!pinned(scene, "giDdgi"))    scene->giDdgi = row.ddgi;
    if (!pinned(scene, "giBounces")) scene->giNumBounces = row.bounces;
    if (!pinned(scene, "giDynamicProbes")) scene->giDynamicProbes = row.dynamicProbes;
    if (!pinned(scene, "giMode")) scene->giMode = iris::GiMode(qBound(1, row.technique, 3));
    else if (scene->giMode == iris::GiMode::OFF) {
        // A pin of "off" is what an Advanced technique picker set to Off would
        // record; turning Rayon on means the tier's technique, so the pin goes.
        scene->worldOverrides.remove(QStringLiteral("giMode"));
        scene->giMode = iris::GiMode(qBound(1, row.technique, 3));
    }
}

bool rayonCustom(const iris::ScenePtr &scene)
{
    return !rayonDeviations(scene).isEmpty();
}

QStringList rayonDeviations(const iris::ScenePtr &scene)
{
    QStringList out;
    // With Rayon off there is nothing to deviate FROM: the picture is "no GI"
    // whatever the machinery rows say, so the tier row reads Off, not Custom.
    if (!scene || !rayonEnabled(scene)) return out;
    const RayonRow &want = kRayonTable[tierIndex(rayonTier(scene))];
    int have[5];
    rayonHave(scene, have);
    const QStringList ids = rayonRowIds();
    for (int i = 0; i < kRayonRowCount; ++i) {
        if (have[i] == rayonColumn(want, i)) continue;
        const Row *r = row(ids[i]);
        out << (r ? r->label : ids[i]);
    }
    return out;
}

void clearRayonOverrides(const iris::ScenePtr &scene)
{
    if (!scene) return;
    for (const QString &id : rayonRowIds()) scene->worldOverrides.remove(id);
    setRayon(scene, rayonEnabled(scene), rayonTier(scene));
}

void deriveRayonFromDocument(const iris::ScenePtr &scene)
{
    if (!scene) return;
    // WHAT THE DOCUMENT RENDERS, read before anything is written.
    const int technique = qBound(0, int(scene->giMode), 3);
    const int quality   = qBound(0, int(scene->giQuality), 2);
    const bool enabled  = technique != 0;

    // THE TIER (spec §2's migration table, at option (b)'s contents): an
    // enabled scene keeps its technique, so the technique picks the tier; an
    // off scene has no technique to speak of, so its quality does. The hybrid
    // derives High, never Epic: Epic's columns (three bounces, dynamic probes)
    // did not exist before the tier table, so no pre-tier document rendered
    // them — a P1-era explicit field opt-in IS the new High row.
    RayonTier tier = RayonTier::Medium;
    if (enabled) {
        tier = technique == 1 ? RayonTier::Low
             : technique == 2 ? RayonTier::Medium
                              : RayonTier::High;
    } else {
        tier = quality == 0 ? RayonTier::Low
             : quality == 1 ? RayonTier::Medium
                            : RayonTier::High;
    }
    scene->giTier = tierIndex(tier);
    const RayonRow &want = kRayonTable[tierIndex(tier)];

    // THE FIELD'S TRI-STATE. -1 in a pre-tier document means the author never
    // touched it — "the tier decides" — and the tier now decides ON at Medium
    // and High (owner option (b): the five shipped vct+medium samples, and the
    // two hybrid ones, come up DDGI-fed; that re-pin is deliberate and is the
    // ONLY rendered value this derivation may move). An explicit 0/1 is what
    // the document rendered and is preserved like every other field.
    if (scene->giDdgi < 0) scene->giDdgi = want.ddgi;

    // PIN WHAT DEVIATES, DROP WHAT DOES NOT. A pin whose value is the tier's
    // own is noise: it would freeze that field through every future tier switch
    // and make the dial look broken, and dropping it changes no value at all.
    int have[5];
    rayonHave(scene, have);
    const QStringList ids = rayonRowIds();
    for (int i = 0; i < kRayonRowCount; ++i) {
        // The technique is never pinned while Rayon is off: OFF is the enable
        // state, not a deviation (setRayon owns that field then).
        const bool skip = (i == 0 && !enabled);
        if (!skip && have[i] != rayonColumn(want, i)) scene->worldOverrides.insert(ids[i], have[i]);
        else                                          scene->worldOverrides.remove(ids[i]);
    }

    // AND THE TIER ROW ITSELF. A scene whose World Mode would resolve Rayon to
    // something else must pin the dial, or the next mode switch (or a reader
    // that re-applies the tier) would silently change how it renders.
    const int rayonValue = enabled ? tierIndex(tier) + 1 : 0;
    // (A scene on Custom has no tier to clobber it, so it needs no pin: it
    // behaves exactly as it did before Rayon existed, including the first time
    // somebody puts it on a World Mode.)
    const Row *r = row(rayonRowId());
    const Mode m = mode(scene);
    if (r && m != Mode::Custom && r->tier[int(m)] != rayonValue)
        scene->worldOverrides.insert(rayonRowId(), rayonValue);
    else
        scene->worldOverrides.remove(rayonRowId());
}

QString modeName(Mode m)
{
    switch (m) {
    case Mode::Low:    return QStringLiteral("low");
    case Mode::Medium: return QStringLiteral("medium");
    case Mode::High:   return QStringLiteral("high");
    case Mode::Epic:   return QStringLiteral("epic");
    case Mode::Custom: break;
    }
    return QStringLiteral("custom");
}

Mode modeFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    if (ok) *ok = true;
    if (n == QLatin1String("low"))    return Mode::Low;
    if (n == QLatin1String("medium")) return Mode::Medium;
    if (n == QLatin1String("high"))   return Mode::High;
    if (n == QLatin1String("epic"))   return Mode::Epic;
    if (n == QLatin1String("custom")) return Mode::Custom;
    if (ok) *ok = false;
    return Mode::Custom;
}

QStringList modeNames()
{
    return { QStringLiteral("low"), QStringLiteral("medium"),
             QStringLiteral("high"), QStringLiteral("epic") };
}

Mode mode(const iris::ScenePtr &scene)
{
    if (!scene) return Mode::Custom;
    const int m = scene->worldMode;
    return (m >= 0 && m <= 3) ? Mode(m) : Mode::Custom;
}

int tierValue(const Row &r, Mode m, const iris::ScenePtr &scene)
{
    // A Rayon-tiered row answers in the RAYON tier space: its columns are the
    // GI dial's tiers, and the World Mode has nothing to say about it directly
    // (it drives the `rayon` row, which drives this one).
    if (r.rayonTiered) return r.tier[int(rayonTier(scene))];
    if (m == Mode::Custom) return resolved(scene, r);
    return r.tier[int(m)];
}

int resolved(const iris::ScenePtr &scene, const Row &r)
{
    // The write-through invariant: the backing field IS the resolved value.
    if (r.get && scene) return r.get(scene);
    // A row with no backing field yet (a declared contract, §9.2) has nowhere to
    // write through TO, so it resolves the long way: an explicit pin, else the
    // current tier's value, else Epic's as the documented shape of the row.
    int v = 0;
    if (overrideValue(scene, r.id, v)) return v;
    const Mode m = mode(scene);
    return m == Mode::Custom ? r.tier[3] : r.tier[int(m)];
}

QString source(const iris::ScenePtr &scene, const Row &r)
{
    if (!scene) return QStringLiteral("custom");
    if (scene->worldOverrides.contains(r.id)) return QStringLiteral("override");
    return mode(scene) == Mode::Custom ? QStringLiteral("custom") : QStringLiteral("mode");
}

namespace {

/// Refuses a value the row cannot hold: an enum row takes only its listed
/// values, an int row only its range, a bool row only 0/1.
bool validate(const Row &r, int value)
{
    switch (r.type) {
    case RowType::Bool: return value == 0 || value == 1;
    case RowType::Int:  return value >= r.minValue && value <= r.maxValue;
    case RowType::Enum:
        for (const EnumOption &o : r.options)
            if (o.value == value) return true;
        return false;
    }
    return false;
}

/// Write-through: the tier value lands in the backing field. A row with no
/// backing field is SKIPPED, deliberately — inserting its tier value into
/// worldOverrides would mark it "pinned by the user" for ever after, and it
/// would then survive every future mode switch (the bug the panel's `*` marker
/// made visible the first time a mode was applied).
void writeField(const iris::ScenePtr &scene, const Row &r, int value)
{
    if (r.set) r.set(scene, value);
}

}   // namespace

void setMode(const iris::ScenePtr &scene, Mode m)
{
    if (!scene) return;
    scene->worldMode = int(m);
    if (m == Mode::Custom) return;   // no tier to write through
    for (const Row &r : rows()) {
        // A Rayon-tiered row is written by the `rayon` row (which IS in this
        // loop and honours the same pins) — never twice, and never from the
        // world tier's columns, which are not this row's tier space at all.
        if (r.rayonTiered) continue;
        if (scene->worldOverrides.contains(r.id)) continue;   // pinned: survives the switch
        writeField(scene, r, r.tier[int(m)]);
    }
}

bool setRowValue(const iris::ScenePtr &scene, const QString &id, int value, bool recordOverride)
{
    if (!scene) return false;
    const Row *r = row(id);
    if (!r || !validate(*r, value)) return false;
    writeField(scene, *r, value);
    // A row with no backing field can ONLY be remembered as a pin, so an
    // explicit set of one always records (it is a deliberate user choice —
    // unlike setMode's sweep, which never touches those rows).
    if (recordOverride || !r->set) scene->worldOverrides.insert(id, value);
    return true;
}

bool setRowValueByOptionId(const iris::ScenePtr &scene, const QString &id, const QString &optionId)
{
    const Row *r = row(id);
    if (!r) return false;
    int value = 0;
    if (!valueFromId(*r, optionId, value)) return false;
    return setRowValue(scene, id, value);
}

void pinRowValue(const iris::ScenePtr &scene, const QString &id, int value)
{
    if (!scene || !row(id)) return;
    scene->worldOverrides.insert(id, value);
}

bool clearOverride(const iris::ScenePtr &scene, const QString &id)
{
    if (!scene || !scene->worldOverrides.contains(id)) return false;
    scene->worldOverrides.remove(id);
    const Row *r = row(id);
    const Mode m = mode(scene);
    // A Rayon-tiered row falls back through the RAYON tier, and it does so via
    // setRayon rather than a bare field write: the technique row doubles as the
    // on/off switch, so writing its tier value directly would switch GI back on
    // for a scene that has it off.
    if (r && r->rayonTiered) {
        setRayon(scene, rayonEnabled(scene), rayonTier(scene));
        return true;
    }
    // Fall back to the tier value. In Custom mode there is no tier, so the
    // field simply keeps whatever it had — dropping the pin is all that happens.
    if (r && m != Mode::Custom) writeField(scene, *r, r->tier[int(m)]);
    return true;
}

void clearOverrides(const iris::ScenePtr &scene)
{
    if (!scene) return;
    const QStringList ids = scene->worldOverrides.keys();
    for (const QString &id : ids) clearOverride(scene, id);
}

QString valueLabel(const Row &r, int value)
{
    if (r.type == RowType::Bool) return value ? QStringLiteral("On") : QStringLiteral("Off");
    for (const EnumOption &o : r.options)
        if (o.value == value) return o.label;
    return QString::number(value);
}

QString valueId(const Row &r, int value)
{
    if (r.type == RowType::Bool) return value ? QStringLiteral("on") : QStringLiteral("off");
    for (const EnumOption &o : r.options)
        if (o.value == value) return o.id;
    return QString::number(value);
}

bool valueFromId(const Row &r, const QString &id, int &out)
{
    const QString n = id.trimmed().toLower();
    if (r.type == RowType::Bool) {
        if (n == QLatin1String("on")  || n == QLatin1String("true")  || n == QLatin1String("1"))
            { out = 1; return true; }
        if (n == QLatin1String("off") || n == QLatin1String("false") || n == QLatin1String("0"))
            { out = 0; return true; }
        return false;
    }
    for (const EnumOption &o : r.options)
        if (o.id == n) { out = o.value; return true; }
    bool ok = false;
    const int v = n.toInt(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

}   // namespace worldmodes
