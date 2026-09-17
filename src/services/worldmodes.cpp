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
#include "jahshaka/engine/Types.h"

#include <algorithm>

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

/// THE PHOTON TIER TABLE (worldmodes.h's comment is the readable form). Per
/// tier: technique (GiMode ordinal), quality (GiQuality ordinal), ddgi (0/1),
/// bounces (total, 1..4). Owner option (b), 2026-09-09; the fifth column
/// (dynamic probes) was deleted with the feature by lane R2, 2026-09-12.
///
/// ONE SOURCE. The four Photon-tiered rows buildRows() declares take their
/// `tier[]` columns FROM this table (photonColumns below) and the public
/// per-column readers (photonTechnique .. photonBounces) read it too, so
/// there is no second copy to drift — gi.tiers asserts the rows against the
/// readers cell by cell (the review found 13 of the 20 cells were untested
/// while they were hand-copied).
/// The fifth column, probeSize, is the REFLECTION-PROBE CAPTURE SIZE in pixels
/// per cube face (owner, 2026-09-13 Q4: "yes halve it but add it to the world
/// settings"). 0 = follow the engine's quality dial, which is what every tier
/// writes today, and the dial is where the sizes themselves live
/// (OgreGi.cpp buildPcc: 128 at Low, 256 at Medium, 512 at High and Epic —
/// the 2026-09-13 halving of High was REVERSED by the owner on 2026-09-15,
/// ledger §324). A number here instead of 0 would move only scenes authored
/// after the change and would make every scene already saved read as Custom,
/// since its stored 0 would no longer match the tier — so the column stays 0
/// and exists for an AUTHOR to pin a size per scene.
/// THE ORDINALS MOVED WITH INSTANT RADIOSITY (PHOTON_SPEC §7 E2 (4)): the
/// technique column is `iris::GiMode` and that enum is now Off 0 / VCT 1 /
/// VCT + Probes 2. Low is a VOXEL tier now — two camera-centred cascades at
/// 64^3 with the irradiance field on and no probes — which is cheaper on the
/// frame than the CPU ray trace it replaces, sees every light instead of one,
/// and is the same arm as the tiers above it.
/// THE SIXTH COLUMN IS THE CASCADE CHAIN, AND IT IS ON IN EVERY TIER
/// (PHOTON_SPEC §7 E2 (6), 2026-09-15). Photon's whole point is that the bounce
/// follows the camera: a single scene-fitted voxel box means a scene bigger than
/// the renderer's 64 m ceiling is voxelised at metres per cell, and a camera
/// that walks out of it walks out of the bounce. The chain is the shipped answer
/// from this tier table on; `world.gi({cascades:false})` is still a per-scene
/// PIN, like every other row here, for anyone who wants the one box back.
struct PhotonRow { int technique, quality, ddgi, bounces, probeSize, cascades; };
const PhotonRow kPhotonTable[4] = {
    /* Low    */ { 1, 0, 1, 1, 0, 1 },   // VCT, 2 cascades @ 64^3, the field on, no probes
    /* Medium */ { 1, 1, 1, 1, 0, 1 },   // VCT, the chain at 64^3, DDGI-fed (voxel source)
    /* High   */ { 2, 2, 1, 1, 0, 1 },   // + probes, the chain's High table, HDR + shadowed
    /* Epic   */ { 2, 2, 1, 3, 0, 1 },   // ... plus 3 bounces
};
/// The Photon-tiered row ids, in kPhotonTable column order.
const int kPhotonRowCount = 6;
int photonColumn(const PhotonRow &r, int i) {
    switch (i) {
    case 0: return r.technique;
    case 1: return r.quality;
    case 2: return r.ddgi;
    case 3: return r.bounces;
    case 4: return r.probeSize;
    default: return r.cascades;
    }
}
/// Fills a Photon-tiered row's four tier cells from the table's column `column`.
void photonColumns(Row &r, int column)
{
    for (int t = 0; t < 4; ++t) r.tier[t] = photonColumn(kPhotonTable[t], column);
}

/// The four tier columns, in order: Low, Medium, High, Epic.
/// Values and reasons come from POST_CHAIN_SPEC.md §9.3, with two documented
/// departures: hardware MSAA is 2x in every tier (the scene under the post chain
/// renders at 1x regardless — see that row — so 2x buys the helpers' and the
/// chain-off scenes' edges for a cost below measurement), and screen-space reflections are DECLARED but not
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
        // 2x in every tier (owner, 2026-09-15, ledger §339, on DEFAULTS-1's
        // measurement). What the number CAN and CANNOT do: with the post chain
        // on, hardware MSAA either crashes the driver (HDR) or renders black
        // (ambient occlusion) — both reproduced in tests/engine — so the chain
        // renders the SCENE at 1x regardless of what is asked here and SMAA
        // smooths its edges. What a multisampled window still anti-aliases is
        // everything drawn INTO the window outside the chain: the gizmos, the
        // light wires and the helpers (measured: 1x vs 2x at Epic moves only
        // the helper-wire pixels), and the whole scene at Low, which runs with
        // the chain off and had no anti-aliasing at all at 1x. 2x costs below
        // the rig's noise on the target GPU; 4x/8x stay a user's choice.
        r.tier[0] = 2; r.tier[1] = 2; r.tier[2] = 2; r.tier[3] = 2;
        r.cost = QStringLiteral("Hardware edge smoothing. Costs render-target memory and "
                                "bandwidth in proportion to the sample count, and the driver may "
                                "clamp the request. While HDR or ambient occlusion is on the "
                                "scene itself renders at 1x and SMAA smooths it; the gizmos, "
                                "light wires and helpers still get this sample count.");
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
                                "curve at the scene's exposure (the Exposure Mode row below says "
                                "whether that is a number or a measurement). The bloom work is at "
                                "a fixed size, so only the final pass scales with the window. Off "
                                "at Low purely to save the buffer.");
        r.get = [](const iris::ScenePtr &s) { return s->hdrEnabled ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { s->hdrEnabled = v != 0; };
        out.append(r);
    }
    {
        // EXPOSURE MODE (EXPOSURE-1, 2026-09-17; RENDER AUDIT A1/A3). A
        // TierSpace::None row: how a scene is exposed is an ART decision, so no
        // World Mode column writes it and a tier switch never regrades anybody's
        // picture — the same rule the continuous parameters have always had.
        // It is a Row rather than a ParamRow because it is a CHOICE of two
        // things, not a number, and because the panel and world.override then
        // get it for free.
        Row r;
        r.id = QStringLiteral("exposureMode");
        r.label = QStringLiteral("Exposure Mode");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::None;
        r.options = { { QStringLiteral("manual"), QStringLiteral("Manual"),
                        int(iris::ExposureMode::Manual) },
                      { QStringLiteral("auto"), QStringLiteral("Auto"),
                        int(iris::ExposureMode::Auto) } };
        r.cost = QStringLiteral("MANUAL (the default) develops the picture at the Exposure below "
                                "and measures nothing: it is exact on the first frame, it is five "
                                "passes and four textures cheaper than the meter, and it is the "
                                "same grade a thumbnail or a screenshot of this world gets. AUTO "
                                "meters the frame and adapts within the window — which is what a "
                                "camera does, and what makes a white surface filling the frame "
                                "darken everything else by about two and a half stops. Use Auto "
                                "when the light in the shot changes and you want the picture to "
                                "follow it.");
        r.get = [](const iris::ScenePtr &s) { return int(s->exposureMode); };
        r.set = [](const iris::ScenePtr &s, int v) {
            s->exposureMode = v == int(iris::ExposureMode::Auto) ? iris::ExposureMode::Auto
                                                                 : iris::ExposureMode::Manual;
        };
        out.append(r);
    }
    {
        // METERING PATTERN (EXPOSURE-2). Beside Exposure Mode, and a
        // TierSpace::None row for the same reason: how a scene is metered is an
        // ART decision, so no quality tier writes it. Shown only under Auto —
        // it is the one thing that would be a LIE under Manual, which measures
        // nothing (the same rule the auto window's pair has).
        Row r;
        r.id = QStringLiteral("exposureMetering");
        r.label = QStringLiteral("Metering");
        r.group = QStringLiteral("Rendering");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::None;
        r.options = { { QStringLiteral("average"), QStringLiteral("Average"),
                        int(iris::ExposureMetering::Average) },
                      { QStringLiteral("centreWeighted"), QStringLiteral("Centre Weighted"),
                        int(iris::ExposureMetering::CentreWeighted) },
                      { QStringLiteral("spot"), QStringLiteral("Spot"),
                        int(iris::ExposureMetering::Spot) } };
        r.visible = [](const iris::ScenePtr &s) {
            return s->exposureMode == iris::ExposureMode::Auto;
        };
        r.cost = QStringLiteral("WHERE the automatic exposure looks. AVERAGE weighs the whole "
                                "frame equally, so a bright sky or a white floor filling most "
                                "of it takes everything else down. CENTRE WEIGHTED (the "
                                "default, and what a camera does) puts about 40% of the "
                                "sensitivity in the middle eleventh of the picture and 81% "
                                "inside the circle that fits its height, with the corners "
                                "still counting for a twentieth. SPOT reads a disc of about "
                                "2.5% of the frame at the centre and nothing else — point it "
                                "at the subject. It costs nothing either way: the pattern is a "
                                "per-pixel weight in the meter's own compute pass.");
        r.get = [](const iris::ScenePtr &s) { return int(s->exposureMetering); };
        r.set = [](const iris::ScenePtr &s, int v) {
            s->exposureMetering = v == int(iris::ExposureMetering::Average)
                                      ? iris::ExposureMetering::Average
                                  : v == int(iris::ExposureMetering::Spot)
                                      ? iris::ExposureMetering::Spot
                                      : iris::ExposureMetering::CentreWeighted;
        };
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
        // THE ROUGHNESS CUTOFF (PHOTON_SPEC §7 R5; owner, ledger §426). It
        // belongs beside the reflection rows because it is the one number that
        // says where reflections stop being worth computing per pixel at all —
        // the same lobe the row above marches and the ray tier traces.
        //
        // WHY IT IS A PROJECT'S NUMBER AND NOT THE RENDERER'S: above the cutoff
        // a reflection is a wide lobe and a reflection probe's prefiltered
        // photograph IS a good integral of it, so a ray or a march per pixel
        // buys a blurrier answer for the same cost. Where that crossover falls
        // depends on the CONTENT — a polished gallery and a weathered street
        // stop being worth tracing at different roughnesses — and no constant
        // can be right for both.
        //
        // The same value in EVERY tier column: it is not a quality trade (the
        // cost of a tier is the resolution and whether rays run at all, both
        // rows of their own), it is a description of the scene's surfaces. A
        // tier change must not silently re-author it.
        Row r;
        r.id = QStringLiteral("reflectionRoughnessCutoff");
        r.label = QStringLiteral("Roughness Cutoff");
        r.group = QStringLiteral("Reflections");
        r.type = RowType::Int;
        r.minValue = 5;
        r.maxValue = 100;
        r.tier[0] = 40; r.tier[1] = 40; r.tier[2] = 40; r.tier[3] = 40;
        // WHAT THIS ROW DRIVES, and it is now BOTH sources (lane SSR-3). The
        // screen-space march used to keep a cutoff of its own
        // (`PostFxDesc::ssrRoughnessCutoff`, 0.35) that nothing in the document
        // could write — and that the marcher compared against the G-buffer's GGX
        // ALPHA without ever square-rooting it, so the band the frame applied
        // was perceptual 0.581, a number nobody chose and nobody could read.
        // That second cutoff is DELETED and the march takes this dial, so the
        // tooltip may say the whole sentence again.
        r.cost = QStringLiteral("How rough a surface may be and still have its reflections "
                                "computed per pixel — MARCHED in screen space and, on a machine "
                                "with ray-tracing hardware, TRACED — in per cent. Below it the "
                                "screen answers what it can see and a ray answers the rest; above "
                                "it the reflection probes' own blurred photograph answers, which "
                                "for a rough surface is both cheaper and closer to the truth. The "
                                "change is feathered: both the screen's march and the ray fade "
                                "out over the same 10 points of this scale below the value, so a surface "
                                "whose roughness varies across it has no seam in it. Raising it "
                                "spends "
                                "rays and marches on surfaces that will look much the same either "
                                "way; lowering it hands more of the picture to the probes.");
        r.available = true;
        r.get = [](const iris::ScenePtr &s) { return s->reflectionRoughnessCutoff; };
        r.set = [](const iris::ScenePtr &s, int v) { s->reflectionRoughnessCutoff = v; };
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
                                "focused map costs R x R of the atlas (at 2048: ~17 MB), so the "
                                "view's atlas runs ~56 MB at 2 lamps, ~88 at 4 and ~160 at 8 — "
                                "and every planar mirror keeps its own half-resolution copy "
                                "(~38 MB at 8) and every shadowed reflection probe a "
                                "quarter-resolution one (~5.5 MB). A point lamp's map is six "
                                "cube-face renders plus a copy, but only in a frame where "
                                "something in its reach changed: a still scene re-renders none "
                                "of them. Empty maps cost no shaders. Lights beyond the budget "
                                "still light the scene; they simply cast no shadow.");
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
        // EPIC IS SOFT, NOT VERY SOFT — fps audit F6. THE REAL TAP COUNTS on
        // this engine pin, read from the shader rather than assumed (they were
        // quoted as 16 vs 36 here for a year, which is the arithmetic of the
        // filter's NAME and not of its loop): OgreHlmsPbs.cpp sets pcf /
        // pcf_iterations and ShadowMapping_piece_ps.any runs them, so Hard =
        // PCF_2x2 = ONE gather-based sample, Soft = PCF_4x4 = 9 taps, Very Soft
        // = PCF_6x6 = 25 taps — per shadowed light, per shaded pixel.
        r.tier[0] = 0; r.tier[1] = 1; r.tier[2] = 1; r.tier[3] = 1;
        r.cost = QStringLiteral("PCF filter width for every shadowed light: Hard (2x2) is 1 "
                                "hardware gather, Soft (4x4) is 9 taps and Very Soft (6x6) is "
                                "25 — per shadowed light, on every pixel it lights, which is why "
                                "no tier asks for Very Soft. The sun costs this three times over, "
                                "once per cascade. Auto uses the softest quality any light in the "
                                "scene asked for. Takes effect next frame; no rebuild.");
        r.get = [](const iris::ScenePtr &s) { return s->shadowFilterTier; };
        r.set = [](const iris::ScenePtr &s, int v) { s->shadowFilterTier = v; };
        out.append(r);
    }

    // ---- Global illumination = PHOTON (GI_UNIFIED_SPEC.md §2) ---------------
    // ONE row is the dial the World Mode drives; the five below it are the
    // machinery that dial consumes, and they are Photon-tiered (Row::tierSpace) — resolved by
    // the PHOTON tier, not by the World Mode, because two dials must never own
    // one backing field. THE TABLE ITSELF is kPhotonTable above; each row's
    // tier[] columns are READ from it (photonColumns), never copied.
    {
        Row r;
        r.id = QStringLiteral("photon");
        r.label = QStringLiteral("Photon (realtime GI)");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.options = { { QStringLiteral("off"),    QStringLiteral("Off"),    0 },
                      { QStringLiteral("low"),    QStringLiteral("Low"),    1 },
                      { QStringLiteral("medium"), QStringLiteral("Medium"), 2 },
                      { QStringLiteral("high"),   QStringLiteral("High"),   3 },
                      { QStringLiteral("epic"),   QStringLiteral("Epic"),   4 } };
        // THE WORLD MODE'S OPINION ABOUT PHOTON:
        //   Low/Medium  -> Photon off
        //   High        -> Photon Low
        //   Epic        -> Photon Epic
        //
        // World High STAYS on Photon Low, and that is a re-decision and not
        // inertia (PHOTON_SPEC E2 (4) asks for one). The column was written
        // when Photon Low WAS Instant Radiosity at low quality — a CPU ray
        // trace from one light. Low is now two camera-centred voxel cascades at
        // 64^3 with the irradiance field on, which is a different technique with
        // the same intent: the cheapest thing that still bounces light
        // everywhere. Moving World High to `off` would take bounced light away
        // from every scene on that mode, and moving it up to Medium would put a
        // 64^3 scene-fitted volume and its probes on a tier whose whole meaning
        // is "not the expensive one". So the mapping is unchanged and what it
        // buys is better.
        r.tier[0] = 0; r.tier[1] = 0; r.tier[2] = 1; r.tier[3] = 4;
        // GENERATED from the two tables (photonTierSummary): this text used to
        // say "Medium voxelizes at twice the resolution" while both tiers were
        // 64 (render audit A5).
        r.cost = QStringLiteral("Photon — realtime global illumination: light that bounces off "
                                "surfaces and colours everything it lands on, recomputed live "
                                "instead of baked. What each tier actually runs — ") +
                 photonTierSummary() +
                 QStringLiteral(" Every knob a tier sets is still reachable one by one under "
                                "Advanced, and anything you set there stays set.");
        r.get = [](const iris::ScenePtr &s) {
            if (!s || s->giMode == iris::GiMode::OFF) return 0;
            return qBound(0, s->giTier, 3) + 1;
        };
        r.set = [](const iris::ScenePtr &s, int v) {
            if (!s) return;
            const bool on = v > 0;
            const PhotonTier t = on ? PhotonTier(qBound(0, v - 1, 3)) : photonTier(s);
            setPhoton(s, on, t);
        };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giMode");
        r.label = QStringLiteral("Photon Technique");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::Photon;
        r.options = { { QStringLiteral("off"),            QStringLiteral("Off"),          0 },
                      { QStringLiteral("vct"),            QStringLiteral("VCT"),          1 },
                      { QStringLiteral("vct_pcc_hybrid"), QStringLiteral("VCT + Probes"), 2 } };
        // PHOTON columns (Low, Medium, High, Epic) — not world-mode ones —
        // read from kPhotonTable. Low and Medium are VCT and differ in the
        // voxel resolution; the top two are the hybrid and differ in the
        // giBounces row below, not here.
        photonColumns(r, 0);
        r.cost = QStringLiteral("Which technique Photon uses. VCT re-voxelizes on "
                                "geometry edits (editing latency, not frame time) and lights from "
                                "everything; VCT + Probes adds sharp reflections near geometry — "
                                "six renders per reflection probe on every re-solve (18 probes by "
                                "default), HDR and shadowed at High quality. Normally the Photon "
                                "quality tier picks this; setting it here PINS it.");
        r.get = [](const iris::ScenePtr &s) { return int(s->giMode); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giMode = iris::GiMode(v); };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giQuality");
        r.label = QStringLiteral("Photon Voxel/Probe Quality");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::Photon;
        r.options = { { QStringLiteral("low"),    QStringLiteral("Low"),    0 },
                      { QStringLiteral("medium"), QStringLiteral("Medium"), 1 },
                      { QStringLiteral("high"),   QStringLiteral("High"),   2 } };
        photonColumns(r, 1);
        // GENERATED: the resolutions and probe sizes are the ENGINE's
        // (giQualityFacts). The hand-written "32/64/128 voxels per axis" was
        // the single-volume arm's numbers, and the cascade chain — on at every
        // tier — uses 64/64/128 (render audit A5).
        r.cost = QStringLiteral("Ray/voxel budget. With Photon's camera cascades on (every tier), "
                                "this dial picks the chain: ") +
                 QStringLiteral("Low %1 voxels per axis, Medium %2, High %3")
                     .arg(photonTierVoxelPhrase(PhotonTier::Low),
                          photonTierVoxelPhrase(PhotonTier::Medium),
                          photonTierVoxelPhrase(PhotonTier::High)) +
                 QStringLiteral("; with the cascades off it sizes the one scene-fitted volume "
                                "instead (%1 / %2 / %3 per axis). It also sets the reflection "
                                "probe's cube face: %4 / %5 / %6 pixels")
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::Low)
                              .voxelResolution)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::Medium)
                              .voxelResolution)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::High)
                              .voxelResolution)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::Low)
                              .probeFaceSize)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::Medium)
                              .probeFaceSize)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::High)
                              .probeFaceSize) +
                 QStringLiteral(". In VCT + Probes High ALSO turns on HDR and shadowed probe "
                                "captures (world.gi's probeHdr/probeShadows pin either one "
                                "independently). High is a re-solve-latency trap in an editor: "
                                "every geometry or light edit pays for it again.");
        r.get = [](const iris::ScenePtr &s) { return int(s->giQuality); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giQuality = iris::GiQuality(v); };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giDdgi");
        r.label = QStringLiteral("Photon Irradiance Field (DDGI)");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::Photon;
        r.options = { { QStringLiteral("off"), QStringLiteral("Off"), 0 },
                      { QStringLiteral("on"),  QStringLiteral("On"),  1 } };
        // Owner option (b), 2026-09-09: every voxel tier feeds the field — it
        // is the one diffuse arm that is right in both open and sealed scenes
        // (rayon2 S1-S3). Low has no voxel volume to feed it from.
        photonColumns(r, 2);
        r.cost = QStringLiteral("The irradiance field: a grid of probes over the lit volume storing "
                                "the bounced light arriving from every direction, plus a depth map "
                                "that decides what each probe can actually see. It is the LEAK FIX "
                                "— cone-traced bounce blows out corners because a cone cannot tell "
                                "a wall from empty space. Turning it on turns the voxel-cone "
                                "diffuse OFF (it replaces that term rather than adding to it); "
                                "reflections, probes and planar are untouched. It needs a voxel "
                                "volume to be fed from, and EVERY Photon tier builds one — "
                                "including Low, whose two camera cascades exist for exactly this "
                                "— so every tier turns it on. (It said \"Low cannot\" here for "
                                "months; Low's column has always been 1.)");
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
    // THE PROBE CAPTURE SIZE (owner decision 2026-09-13 Q4: "yes halve it but
    // add it to the world settings"). A Photon-tiered row like the four above, so
    // an edit PINS it; every tier's column is 0 = "follow the quality dial",
    // because the halving itself is the engine's default now.
    {
        Row r;
        r.id = QStringLiteral("giProbeSize");
        r.label = QStringLiteral("Photon Probe Capture Size");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::Photon;
        r.options = { { QStringLiteral("auto"), QStringLiteral("Automatic"), 0 },
                      { QStringLiteral("128"),  QStringLiteral("128 px"),  128 },
                      { QStringLiteral("256"),  QStringLiteral("256 px"),  256 },
                      { QStringLiteral("512"),  QStringLiteral("512 px"),  512 } };
        photonColumns(r, 4);
        // GENERATED: "Automatic" is the engine's quality dial and only two
        // tiers build probes at all. The hand-written version claimed 256 "at
        // every tier from Medium up" while High and Epic resolve 512, and
        // Medium builds no probe grid to size (render audit A5).
        r.cost = QStringLiteral("The pixel size of ONE reflection-probe cube face. A probe is six "
                                "of them plus a mip chain, so the grid's video memory goes with "
                                "the SQUARE of this: at 256 a probe is 4.0 MB in HDR and a "
                                "32-probe room 128 MB; at 512 it is 16.0 MB and 512 MB. "
                                "Automatic follows the quality dial (%1 px at Low, %2 at Medium, "
                                "%3 at High and Epic), and only High and Epic build a probe grid "
                                "at all, so %3 is the shipped answer wherever this row has "
                                "anything to size. The roughness blur the renderer convolves "
                                "into these captures hides the difference on everything but a "
                                "mirror.")
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::Low)
                              .probeFaceSize)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::Medium)
                              .probeFaceSize)
                     .arg(jahshaka::engine::giQualityFacts(jahshaka::engine::GiQuality::High)
                              .probeFaceSize);
        r.get = [](const iris::ScenePtr &s) { return qBound(0, s->giProbeCaptureSize, 1024); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giProbeCaptureSize = qBound(0, v, 1024); };
        out.append(r);
    }
    // THE CASCADE CHAIN, as a tier row (PHOTON_SPEC §7 E2 (6)). A Photon-tiered
    // Enum like the technique above it, so an edit here PINS the chain on or off
    // against the tier, and every tier's column is 1.
    {
        Row r;
        r.id = QStringLiteral("giCascades");
        r.label = QStringLiteral("Photon Camera Cascades");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Enum;
        r.tierSpace = TierSpace::Photon;
        r.options = { { QStringLiteral("off"), QStringLiteral("Off"), 0 },
                      { QStringLiteral("on"),  QStringLiteral("On"),  1 } };
        photonColumns(r, 5);
        r.cost = QStringLiteral("Where the voxels are. On (every tier) the renderer builds a chain "
                                "of voxel boxes CENTRED ON THE CAMERA — fine cells near the eye, "
                                "coarse ones far away — and re-centres them as you travel, at most "
                                "one box per frame, so bounced light follows you through a world of "
                                "any size and what escapes the outermost box reads the Sky Light "
                                "rather than going dark. Off fits ONE box around the scene's "
                                "content instead, under a 64 m ceiling: inside it the bounce is "
                                "identical and slightly cheaper per pixel (one volume to cone-trace "
                                "instead of four), outside it there is no bounce at all, and a "
                                "scene larger than the ceiling is voxelised at metres per cell. "
                                "Off is the right answer for one room that the camera stays inside; "
                                "On is the right answer for everything else.");
        r.get = [](const iris::ScenePtr &s) { return s && s->giCascades > 0 ? 1 : 0; };
        r.set = [](const iris::ScenePtr &s, int v) { if (s) s->giCascades = v ? 1 : 0; };
        out.append(r);
    }
    {
        Row r;
        r.id = QStringLiteral("giBounces");
        r.label = QStringLiteral("Photon Light Bounces");
        r.group = QStringLiteral("Global Illumination");
        r.type = RowType::Int;
        r.tierSpace = TierSpace::Photon;
        r.minValue = 1; r.maxValue = 4;
        photonColumns(r, 3);
        // GENERATED: "128^3 at High/Epic" named one resolution for a chain
        // whose four cascades are 128, 128, 64 and 64 (render audit A5).
        r.cost = QStringLiteral("Total light bounces, 1-4. Each bounce past the first is another "
                                "light-propagation pass over EVERY voxel volume on every "
                                "re-solve — at High and Epic that is a chain at %1 voxels per "
                                "axis — and the irradiance field is fed from those volumes, so "
                                "the extra bounces reach the probe-stored diffuse too. Epic's "
                                "column: %2; every other tier %3.")
                     .arg(photonTierVoxelPhrase(PhotonTier::High))
                     .arg(photonBounces(PhotonTier::Epic))
                     .arg(photonBounces(PhotonTier::Low));
        r.get = [](const iris::ScenePtr &s) { return qBound(1, s->giNumBounces, 4); };
        r.set = [](const iris::ScenePtr &s, int v) { s->giNumBounces = qBound(1, v, 4); };
        out.append(r);
    }
    // ---- Sky ---------------------------------------------------------------
    // THE "Sky Detail" ROW IS GONE (SKY-GPU, 2026-09-13). It was the width the
    // analytic sky was CPU-BAKED at, and there is no bake: the sky is a shader.
    // Nothing replaced it — a tier row has to buy something, and this one now
    // buys nothing.

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
    // ---- EXPOSURE (EXPOSURE-1, 2026-09-17) --------------------------------
    //
    // STOPS, all three, on the same axis a camera's block uses. The post chain
    // takes a natural-log `E` and the conversion happens once, at the mirror
    // (iris::lens::toChain) — nothing in the document or in this table holds a
    // chain-unit exposure any more. The `Ev` in the id is what says so, and it
    // is what keeps a script written against the old chain-unit `exposure` from
    // silently regrading a scene: that name is gone and the verb refuses it.
    {
        ParamRow p;
        p.id = QStringLiteral("exposureEv");
        p.label = QStringLiteral("Exposure");
        p.ownerRowId = QStringLiteral("exposureMode");
        p.minValue = -16.0; p.maxValue = 16.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("The exposure in STOPS: 0 is the grade a new scene's own lights "
                               "derive (a sun and a Sky Light at intensity 1 over the default "
                               "sky put an 18% grey card on the film's grey card), +1 is one "
                               "doubling, -1 one halving. In Manual it IS the exposure; in Auto "
                               "it is the grade the meter works around, and the window below "
                               "says how far either side of it the meter may go. The same unit "
                               "as a camera's own exposure block.");
        p.enabled = [](const iris::ScenePtr &s) { return s->hdrEnabled; };
        p.get = [](const iris::ScenePtr &s) { return double(s->exposure); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposure = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("exposureMin");
        p.label = QStringLiteral("Auto Exposure Min");
        p.ownerRowId = QStringLiteral("exposureMode");
        p.minValue = -16.0; p.maxValue = 16.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("The bottom of the window the AUTOMATIC exposure may adapt "
                               "within, in stops AROUND THE EXPOSURE ABOVE: -3.5 lets the meter "
                               "land up to three and a half stops under the exposure you typed, "
                               "and 0 stops means it may not go under it at all. A window of "
                               "0..0 is the exposure you typed and nothing else — the same "
                               "picture Manual renders. Read only in Auto: Manual measures "
                               "nothing, so there is nothing to bound.");
        p.enabled = [](const iris::ScenePtr &s) { return s->hdrEnabled; };
        p.visible = [](const iris::ScenePtr &s) {
            return s->exposureMode == iris::ExposureMode::Auto;
        };
        p.get = [](const iris::ScenePtr &s) { return double(s->exposureMin); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposureMin = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("exposureMax");
        p.label = QStringLiteral("Auto Exposure Max");
        p.ownerRowId = QStringLiteral("exposureMode");
        p.minValue = -16.0; p.maxValue = 16.0; p.perPixelStep = 0.01; p.decimals = 2;
        p.doc = QStringLiteral("The top of the automatic exposure's window, in stops above the "
                               "exposure above. A narrow window is a steadier image; a wide one "
                               "copes with walking from a dark room into daylight. Read only in "
                               "Auto.");
        p.enabled = [](const iris::ScenePtr &s) { return s->hdrEnabled; };
        p.visible = [](const iris::ScenePtr &s) {
            return s->exposureMode == iris::ExposureMode::Auto;
        };
        p.get = [](const iris::ScenePtr &s) { return double(s->exposureMax); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposureMax = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("exposureMeterLow");
        p.label = QStringLiteral("Meter Low Percentile");
        p.ownerRowId = QStringLiteral("exposureMode");
        p.minValue = 0.0; p.maxValue = 100.0; p.perPixelStep = 0.25; p.decimals = 1;
        p.doc = QStringLiteral("WHICH SLICE of what the meter sees it believes, from the dark "
                               "end: 10 throws away the darkest tenth of the metered weight. "
                               "The meter builds a histogram of the frame's log luminance and "
                               "averages between this percentile and the high one, so a deep "
                               "shadow region cannot pull the grade the way it pulls an "
                               "average. 0 keeps everything. Read only in Auto.");
        p.enabled = [](const iris::ScenePtr &s) { return s->hdrEnabled; };
        p.visible = [](const iris::ScenePtr &s) {
            return s->exposureMode == iris::ExposureMode::Auto;
        };
        p.get = [](const iris::ScenePtr &s) { return double(s->exposureMeterLowPercent); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposureMeterLowPercent = float(v); };
        out.append(p);
    }
    {
        ParamRow p;
        p.id = QStringLiteral("exposureMeterHigh");
        p.label = QStringLiteral("Meter High Percentile");
        p.ownerRowId = QStringLiteral("exposureMode");
        p.minValue = 0.0; p.maxValue = 100.0; p.perPixelStep = 0.25; p.decimals = 1;
        p.doc = QStringLiteral("The same from the bright end: 90 throws away the brightest "
                               "tenth of the metered weight, which is what keeps a sun disc, "
                               "a blown window or a specular firefly out of the measurement. "
                               "100 keeps everything, and is the closest this meter gets to "
                               "the plain mean it replaced. A pair that keeps nothing is read "
                               "as the whole frame. Read only in Auto.");
        p.enabled = [](const iris::ScenePtr &s) { return s->hdrEnabled; };
        p.visible = [](const iris::ScenePtr &s) {
            return s->exposureMode == iris::ExposureMode::Auto;
        };
        p.get = [](const iris::ScenePtr &s) { return double(s->exposureMeterHighPercent); };
        p.set = [](const iris::ScenePtr &s, double v) { s->exposureMeterHighPercent = float(v); };
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
        QStringLiteral("hdr"), QStringLiteral("exposureMode"),
        QStringLiteral("exposureMetering"), QStringLiteral("bloom"),
        QStringLiteral("ssao"),
        QStringLiteral("smaa"), QStringLiteral("ssr"), QStringLiteral("refractions"),
        QStringLiteral("distortion"),
    };
    return ids;
}

// ---------------------------------------------------------------------------
// PHOTON (GI_UNIFIED_SPEC.md §2 / P2) — the unified realtime-GI dial.
//
// Everything below manipulates the three backing fields DIRECTLY rather than
// going through setRowValue(), for two reasons: applying a tier must not record
// pins (setMode's rule, and the bug the panel's `*` marker made visible), and
// the `photon` row's own set() runs from inside the registry — reaching back
// into rows() from there is a re-entrancy nobody should have to reason about.
// The tier table itself lives ABOVE buildRows (the rows derive their columns
// from it); what follows is the bookkeeping that reads it.

namespace {

int tierIndex(PhotonTier t) { return qBound(0, int(t), 3); }

bool pinned(const iris::ScenePtr &s, const char *id)
{
    return s && s->worldOverrides.contains(QLatin1String(id));
}

}   // namespace

QString photonRowId() { return QStringLiteral("photon"); }

QStringList photonRowIds()
{
    return { QStringLiteral("giMode"), QStringLiteral("giQuality"),
             QStringLiteral("giDdgi"), QStringLiteral("giBounces"),
             QStringLiteral("giProbeSize"), QStringLiteral("giCascades") };
}

QString photonTierName(PhotonTier t)
{
    static const char *names[4] = { "low", "medium", "high", "epic" };
    return QString::fromLatin1(names[tierIndex(t)]);
}

PhotonTier photonTierFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    if (ok) *ok = true;
    if (n == QLatin1String("low"))    return PhotonTier::Low;
    if (n == QLatin1String("medium")) return PhotonTier::Medium;
    if (n == QLatin1String("high"))   return PhotonTier::High;
    if (n == QLatin1String("epic"))   return PhotonTier::Epic;
    if (ok) *ok = false;
    return PhotonTier::Epic;
}

QStringList photonTierNames()
{
    return { QStringLiteral("low"), QStringLiteral("medium"),
             QStringLiteral("high"), QStringLiteral("epic") };
}

PhotonTier photonTier(const iris::ScenePtr &scene)
{
    if (!scene) return PhotonTier::Epic;
    return PhotonTier(qBound(0, scene->giTier, 3));
}

bool photonEnabled(const iris::ScenePtr &scene)
{
    return scene && scene->giMode != iris::GiMode::OFF;
}

int photonTechnique(PhotonTier t) { return kPhotonTable[tierIndex(t)].technique; }
int photonQuality(PhotonTier t)   { return kPhotonTable[tierIndex(t)].quality; }
int photonDdgi(PhotonTier t)      { return kPhotonTable[tierIndex(t)].ddgi; }
int photonBounces(PhotonTier t)   { return kPhotonTable[tierIndex(t)].bounces; }
int photonProbeSize(PhotonTier t) { return kPhotonTable[tierIndex(t)].probeSize; }
int photonCascades(PhotonTier t)  { return kPhotonTable[tierIndex(t)].cascades; }

// ---------------------------------------------------------------------------
// WHAT A TIER IS, IN WORDS, GENERATED (render audit A5).
//
// Five tooltips in this file used to describe a renderer that did not exist —
// "Medium voxelizes at twice the resolution" (both are 64), "32/64/128 voxels
// per axis" (the cascade chain, on at every tier, is 64/64/128), "256 at every
// tier from Medium up" (High and Epic are 512), "128^3 at High/Epic" (two of
// the four cascades are 64) and "Low cannot [feed the field]" (Low's column has
// always been 1). Every one of them was a HAND COPY of a number that lives
// somewhere else. These functions read the two tables instead: kPhotonTable
// above, and the engine's `giQualityFacts`.
namespace {

QString metres(float v)
{
    // Two decimals, trailing zeros trimmed: "5", "0.16", "1.88".
    QString s = QString::number(double(v), 'f', 2);
    while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
        s.chop(1);
    return s;
}

/// The engine's physical facts for a tier's quality column.
jahshaka::engine::GiQualityFacts factsFor(PhotonTier t)
{
    return jahshaka::engine::giQualityFacts(
        jahshaka::engine::GiQuality(qBound(0, photonQuality(t), 2)));
}

}   // namespace

QString photonTierVoxelPhrase(PhotonTier t)
{
    const auto facts = factsFor(t);
    if (!photonCascades(t) || facts.cascadeCount <= 0)
        return QString::number(facts.voxelResolution);
    QList<int> seen;
    for (int i = 0; i < facts.cascadeCount; ++i)
        if (!seen.contains(facts.cascades[i].resolution)) seen.append(facts.cascades[i].resolution);
    std::sort(seen.begin(), seen.end());
    QStringList parts;
    for (int r : seen) parts << QString::number(r);
    if (parts.size() == 1) return parts.first();
    const QString last = parts.takeLast();
    return parts.join(QStringLiteral(", ")) + QStringLiteral(" and ") + last;
}

int photonTierProbeFaceSize(PhotonTier t)
{
    const int pinned = photonProbeSize(t);
    return pinned > 0 ? pinned : int(factsFor(t).probeFaceSize);
}

QString photonTierSentence(PhotonTier t)
{
    const auto facts = factsFor(t);
    QString out = photonTierName(t);
    out[0] = out[0].toUpper();
    out += QStringLiteral(": ");

    if (photonCascades(t) && facts.cascadeCount > 0) {
        QStringList rows;
        for (int i = 0; i < facts.cascadeCount; ++i) {
            const auto &c = facts.cascades[i];
            rows << QStringLiteral("%1 m at %2 cubed (%3 m cells)")
                        .arg(metres(c.halfSize))
                        .arg(c.resolution)
                        .arg(metres(jahshaka::engine::giCascadeCell(c)));
        }
        out += QStringLiteral("%1 camera-centred voxel cascade%2 — %3")
                   .arg(facts.cascadeCount)
                   .arg(facts.cascadeCount == 1 ? QString() : QStringLiteral("s"))
                   .arg(rows.join(QStringLiteral(", ")));
    } else {
        out += QStringLiteral("one scene-fitted voxel volume at %1 cubed")
                   .arg(facts.voxelResolution);
    }

    out += photonDdgi(t) ? QStringLiteral("; the irradiance field ON")
                         : QStringLiteral("; no irradiance field");
    out += QStringLiteral("; %1 light bounce%2")
               .arg(photonBounces(t))
               .arg(photonBounces(t) == 1 ? QString() : QStringLiteral("s"));

    // The probe grid is the TECHNIQUE column, not the quality one: only the
    // hybrid (ordinal 2) builds one.
    if (photonTechnique(t) == 2) {
        out += QStringLiteral("; a reflection-probe grid at %1 px per cube face")
                   .arg(photonTierProbeFaceSize(t));
        if (facts.probeHdrDefault) out += QStringLiteral(", HDR");
        if (facts.probeShadowsDefault) out += QStringLiteral(", shadowed");
    } else {
        out += QStringLiteral("; no reflection probes");
    }
    return out + QStringLiteral(".");
}

QString photonTierSummary()
{
    QStringList lines;
    for (const QString &name : photonTierNames()) {
        bool ok = false;
        const PhotonTier t = photonTierFromName(name, &ok);
        if (ok) lines << photonTierSentence(t);
    }
    return lines.join(QStringLiteral(" "));
}

namespace {
/// The four values a scene RENDERS, in kPhotonTable column order.
void photonHave(const iris::ScenePtr &s, int have[kPhotonRowCount])
{
    have[0] = int(s->giMode);
    have[1] = int(s->giQuality);
    have[2] = s->giDdgi > 0 ? 1 : 0;
    have[3] = qBound(1, s->giNumBounces, 4);
    have[4] = qBound(0, s->giProbeCaptureSize, 1024);
    have[5] = s->giCascades > 0 ? 1 : 0;
}
}   // namespace

void setPhoton(const iris::ScenePtr &scene, bool enabled, PhotonTier tier)
{
    if (!scene) return;
    const PhotonRow &row = kPhotonTable[tierIndex(tier)];
    scene->giTier = tierIndex(tier);

    if (!enabled) {
        // OFF writes exactly ONE field. The technique field IS the on/off
        // switch (there is no second flag to disagree with the renderer), so a
        // pinned technique cannot survive an off — "off, but pinned to VCT" is
        // a state nothing can render, and the pin would silently un-switch
        // Photon at the next tier application. The machinery rows are left
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
    if (!pinned(scene, "giProbeSize")) scene->giProbeCaptureSize = qBound(0, row.probeSize, 1024);
    if (!pinned(scene, "giCascades")) scene->giCascades = row.cascades;
    if (!pinned(scene, "giMode")) scene->giMode = iris::GiMode(qBound(1, row.technique, 2));
    else if (scene->giMode == iris::GiMode::OFF) {
        // A pin of "off" is what an Advanced technique picker set to Off would
        // record; turning Photon on means the tier's technique, so the pin goes.
        scene->worldOverrides.remove(QStringLiteral("giMode"));
        scene->giMode = iris::GiMode(qBound(1, row.technique, 2));
    }
}

bool photonCustom(const iris::ScenePtr &scene)
{
    return !photonDeviations(scene).isEmpty();
}

QStringList photonDeviations(const iris::ScenePtr &scene)
{
    QStringList out;
    // With Photon off there is nothing to deviate FROM: the picture is "no GI"
    // whatever the machinery rows say, so the tier row reads Off, not Custom.
    if (!scene || !photonEnabled(scene)) return out;
    const PhotonRow &want = kPhotonTable[tierIndex(photonTier(scene))];
    int have[kPhotonRowCount];
    photonHave(scene, have);
    const QStringList ids = photonRowIds();
    for (int i = 0; i < kPhotonRowCount; ++i) {
        if (have[i] == photonColumn(want, i)) continue;
        const Row *r = row(ids[i]);
        out << (r ? r->label : ids[i]);
    }
    return out;
}

void clearPhotonOverrides(const iris::ScenePtr &scene)
{
    if (!scene) return;
    for (const QString &id : photonRowIds()) scene->worldOverrides.remove(id);
    setPhoton(scene, photonEnabled(scene), photonTier(scene));
}

void derivePhotonFromDocument(const iris::ScenePtr &scene)
{
    if (!scene) return;
    // WHAT THE DOCUMENT RENDERS, read before anything is written.
    const int technique = qBound(0, int(scene->giMode), 2);
    const int quality   = qBound(0, int(scene->giQuality), 2);
    const bool enabled  = technique != 0;

    // THE TIER (spec §2's migration table, at option (b)'s contents): an
    // enabled scene keeps its technique, so the technique picks the tier; an
    // off scene has no technique to speak of, so its quality does. The hybrid
    // derives High, never Epic: Epic's column (three bounces) did not exist
    // before the tier table, so no pre-tier document rendered it — a P1-era
    // explicit field opt-in IS the new High row.
    // (THE TECHNIQUE ORDINALS MOVED with Instant Radiosity's deletion — VCT is
    // 1 and the hybrid 2 — so VCT alone can no longer tell Low from Medium.
    // The QUALITY does, and it is the field those two tiers actually differ in:
    // Low is 32^3 (the cascade chain's own Low row is 64), Medium 64^3.)
    PhotonTier tier = PhotonTier::Medium;
    if (enabled) {
        tier = technique == 2 ? PhotonTier::High
             : quality == 0   ? PhotonTier::Low
                              : PhotonTier::Medium;
    } else {
        tier = quality == 0 ? PhotonTier::Low
             : quality == 1 ? PhotonTier::Medium
                            : PhotonTier::High;
    }
    scene->giTier = tierIndex(tier);
    const PhotonRow &want = kPhotonTable[tierIndex(tier)];

    // THE FIELD'S TRI-STATE. -1 in a pre-tier document means the author never
    // touched it — "the tier decides" — and the tier now decides ON at Medium
    // and High (owner option (b): the five shipped vct+medium samples, and the
    // two hybrid ones, come up DDGI-fed; that re-pin is deliberate and is the
    // ONLY rendered value this derivation may move). An explicit 0/1 is what
    // the document rendered and is preserved like every other field.
    if (scene->giDdgi < 0) scene->giDdgi = want.ddgi;
    // ...and the CASCADE column, the same way and for the same reason: -1 means
    // the file predates the column (PHOTON_SPEC §7 E2 (6)), so it follows the
    // tier — which is ON — rather than reading as a deviation nobody authored.
    if (scene->giCascades < 0) scene->giCascades = want.cascades;

    // PIN WHAT DEVIATES, DROP WHAT DOES NOT. A pin whose value is the tier's
    // own is noise: it would freeze that field through every future tier switch
    // and make the dial look broken, and dropping it changes no value at all.
    int have[kPhotonRowCount];
    photonHave(scene, have);
    const QStringList ids = photonRowIds();
    for (int i = 0; i < kPhotonRowCount; ++i) {
        // The technique is never pinned while Photon is off: OFF is the enable
        // state, not a deviation (setPhoton owns that field then).
        const bool skip = (i == 0 && !enabled);
        if (!skip && have[i] != photonColumn(want, i)) scene->worldOverrides.insert(ids[i], have[i]);
        else                                          scene->worldOverrides.remove(ids[i]);
    }

    // AND THE TIER ROW ITSELF. A scene whose World Mode would resolve Photon to
    // something else must pin the dial, or the next mode switch (or a reader
    // that re-applies the tier) would silently change how it renders.
    const int photonValue = enabled ? tierIndex(tier) + 1 : 0;
    // (A scene on Custom has no tier to clobber it, so it needs no pin: it
    // behaves exactly as it did before Photon existed, including the first time
    // somebody puts it on a World Mode.)
    const Row *r = row(photonRowId());
    const Mode m = mode(scene);
    if (r && m != Mode::Custom && r->tier[int(m)] != photonValue)
        scene->worldOverrides.insert(photonRowId(), photonValue);
    else
        scene->worldOverrides.remove(photonRowId());
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
    // A Photon-tiered row answers in the PHOTON tier space: its columns are the
    // GI dial's tiers, and the World Mode has nothing to say about it directly
    // (it drives the `photon` row, which drives this one).
    if (r.tierSpace == TierSpace::Photon) return r.tier[int(photonTier(scene))];
    // NO TIER RESOLVES THIS ROW (TierSpace::None): its value is the backing field.
    if (r.tierSpace == TierSpace::None) return resolved(scene, r);
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
    // A row NO TIER RESOLVES was never set by a mode, so saying "mode" would be
    // a claim about a dial that does not own it (EXPOSURE-1).
    if (r.tierSpace == TierSpace::None) return QStringLiteral("custom");
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
        // A Photon-tiered row is written by the `photon` row (which IS in this
        // loop and honours the same pins) — never twice, and never from the
        // world tier's columns, which are not this row's tier space at all.
        if (r.tierSpace != TierSpace::World) continue;
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
    // A Photon-tiered row falls back through the PHOTON tier, and it does so via
    // setPhoton rather than a bare field write: the technique row doubles as the
    // on/off switch, so writing its tier value directly would switch GI back on
    // for a scene that has it off.
    if (r && r->tierSpace == TierSpace::Photon) {
        setPhoton(scene, photonEnabled(scene), photonTier(scene));
        return true;
    }
    // A ROW NO TIER RESOLVES HAS NOTHING TO FALL BACK TO, and `r->tier[]` is a
    // block of zeros nobody ever filled in — writing it would silently reset
    // the field to whatever zero happens to mean (for Exposure Mode: MANUAL, so
    // an Auto scene would flip the moment ANY pin anywhere was cleared, since
    // clearOverrides walks the whole map). Dropping the pin is the whole
    // operation for these rows.
    if (r && r->tierSpace == TierSpace::None) return true;
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
    // CASE-INSENSITIVE, and the caller's spelling is what is being forgiven —
    // not the table's. Every option id was lowercase until EXPOSURE-2 gave the
    // metering row `centreWeighted`, which is the DOCUMENT's own serialised
    // spelling (iris::exposureMeteringName) and must stay that on both surfaces;
    // comparing against a lowercased `n` silently refused it. Every existing id
    // is unaffected, being lowercase already.
    for (const EnumOption &o : r.options)
        if (o.id.compare(n, Qt::CaseInsensitive) == 0) { out = o.value; return true; }
    bool ok = false;
    const int v = n.toInt(&ok);
    if (!ok) return false;
    out = v;
    return true;
}

}   // namespace worldmodes
