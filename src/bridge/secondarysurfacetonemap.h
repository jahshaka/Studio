/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SECONDARYSURFACETONEMAP_H
#define SECONDARYSURFACETONEMAP_H

// THE SECONDARY-SURFACE TONEMAP (owner report 2026-09-07, fix wave item 6).
//
// THE DEFECT. Every surface that is not the main viewport — asset thumbnails,
// material and asset previews, the screenshot dialog — renders the scene's raw
// linear radiance into an 8-bit target and reads it straight back. The viewport
// beside it tonemaps (HDR is on from Medium upwards). So a world that looks
// graded on screen photographs BLOWN OUT: anything above 1.0 clips to flat
// white, and the brighter the light the worse the thumbnail.
//
// THE FIX, in one line: `hdr` + `tonemapFixed` + `allowOffscreen`, and nothing
// else. Deliberately NOT the whole chain the viewport runs —
//
//   * bloom, SSAO and SMAA are LOOK, and a thumbnail is a photograph of the
//     content, not of the scene's quality tier. They also cost real time on a
//     surface that renders hundreds of times during an import sweep.
//   * `tonemapFixed` (Types.h) is what makes it assertable: the automatic
//     exposure adapts over FRAMES against WALL-CLOCK time and rides
//     process-global material parameters, so on a two-frame readback its result
//     is unrepeatable. The fixed form replaces the measurement with a constant
//     derived from `exposure` — the same filmic curve, the same node, the same
//     answer every time.
//
// THE RULE THIS DOES NOT BREAK: the offscreen determinism guarantee
// (POST_CHAIN_SPEC §7.3) stands exactly as written. An offscreen View still
// ignores the post chain unless something OPTS IN, and the opt-in is per
// surface. Every existing pixel suite renders through a view nobody calls this
// on, so every existing pixel assertion is byte-for-byte what it was.

#include "jahshaka/engine/Engine.h"

namespace secondaryfx {

/// THE THUMBNAIL GRADE. Turns the deterministic tonemap on (or back off) for an
/// OFFSCREEN view, at the SCENE's exposure — which every caller now passes, and
/// which is SS1's one-line half of this file: a thumbnail of a world the user
/// had regraded used to be a picture of the ungraded one, because this took a
/// hardcoded +0.6 instead. `exposure` has NO DEFAULT on purpose (the old
/// kFixedExposure constant is deleted): a caller with no scene to ask is a
/// caller that has not thought about it.
/// Safe on a null view; safe to call every frame (an unchanged PostFxDesc never
/// reaches the backend — OgreView::setPostFx early-outs on equality).
inline void apply(jahshaka::engine::View *view, bool enabled, float exposure)
{
    if (!view) return;
    jahshaka::engine::PostFxDesc fx = view->postFx();
    // Only the three fields this surface is entitled to. Anything else the
    // scene's post-fx description pushed (bloom, SSAO, SMAA, SSR) stays off,
    // because `allowOffscreen` is what would have let it through and it is set
    // here alongside a deliberately minimal chain.
    fx.hdr          = enabled;
    fx.tonemapFixed = enabled;
    fx.exposure     = exposure;
    fx.bloom        = false;
    fx.ssao         = false;
    fx.smaaPreset   = -1;
    fx.ssr          = 0;
    fx.allowOffscreen = enabled;
    view->setPostFx(fx);
}

/// THE EDITOR'S OWN PICTURE (SS1, owner 2026-09-13: "match the screenshot to
/// the scene properly").
///
/// The function above develops a THUMBNAIL and is right for one. It was also,
/// wrongly, what a user pressing Screenshot got: the diagnosis measured a
/// viewport-vs-shot RMSE of 0.22 on the Grand Showroom, 100% of it the post
/// chain and the exposure — SSAO, SSR, bloom and SMAA simply absent, the mirror
/// floor gone, the World's exposure ignored (0.6 -> 2.4 moved the viewport's
/// mean 63.9 -> 187.9 and the shot not at all) and the filmic grade applied
/// even to a world with HDR switched OFF.
///
/// THIS IS THE OTHER ANSWER, and it is three decisions:
///
///  1. THE WHOLE CHAIN, AS THE WORLD HAS IT. `allowOffscreen` and nothing else
///     — the description this view is already carrying is the one applyEnvironment
///     and applyCamera pushed, i.e. the viewport's. We add no row and remove no
///     row, so "if the viewport shows it, the shot shows it" is structural
///     rather than a list somebody has to keep up to date.
///  2. HDR STAYS OFF WHEN THE WORLD HAS IT OFF. The thumbnail grade forces it
///     on (a thumbnail of a bright scene has to survive the 8-bit readback);
///     a screenshot must not, or a Low-tier world photographs graded while the
///     viewport beside it does not.
///  3. THE EXPOSURE IS PINNED TO THE MEASURED ONE. The automatic exposure is a
///     temporal filter that converges at ~75%/s and this view lives two frames,
///     so it can NEVER converge — re-seeding it (the `Viewport` grade) only
///     means grading at the seed. The on-screen view HAS converged, on this
///     same scene, and View::measuredExposureScale reads that number straight
///     out of the chain's 1x1 history. Handing it over as `exposureScale` +
///     `tonemapFixed` uses the identical tonemap node and curve with the
///     measurement already made: right, and deterministic, at the same time.
///     `measured <= 0` (no on-screen view, no HDR on screen, nothing presented
///     yet) falls back to `exposure`'s grey card — the old behaviour, never an
///     ungraded surprise.
inline void applyScene(jahshaka::engine::View *view, float measuredExposureScale) {
    if (!view) return;
    jahshaka::engine::PostFxDesc fx = view->postFx();
    fx.allowOffscreen = true;
    if (fx.hdr) {
        fx.tonemapFixed  = true;
        fx.exposureScale = measuredExposureScale > 0.0f ? measuredExposureScale : 0.0f;
    }
    view->setPostFx(fx);
}

}   // namespace secondaryfx

#endif // SECONDARYSURFACETONEMAP_H
