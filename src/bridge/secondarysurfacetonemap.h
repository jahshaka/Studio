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

/// The exposure the secondary surfaces grade at. iris::Scene's own default
/// (scene.cpp: "+0.6, not 0" — the amount that puts mid-grey back where it was
/// when HDR comes on), so a thumbnail of a scene nobody has regraded matches
/// the viewport beside it.
constexpr float kFixedExposure = 0.6f;

/// Turns the deterministic tonemap on (or back off) for an OFFSCREEN view.
/// Safe on a null view; safe to call every frame (an unchanged PostFxDesc never
/// reaches the backend — OgreView::setPostFx early-outs on equality).
inline void apply(jahshaka::engine::View *view, bool enabled,
                  float exposure = kFixedExposure)
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

}   // namespace secondaryfx

#endif // SECONDARYSURFACETONEMAP_H
