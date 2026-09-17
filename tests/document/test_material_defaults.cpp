// document.material_defaults — THE UNAUTHORED SURFACE IS A PHYSICAL ONE, AND IT
// IS NOT ON A TIER'S THRESHOLD (DRAG-1, from the render audit's IRISGL I-1 and
// APP A2).
//
// WHAT WAS WRONG. `PbrMaterial`'s constructor invented albedo sRGB 255 — 1.0
// LINEAR, a surface that returns every photon it receives, brighter than fresh
// snow (0.80-0.90) and than white paint (0.80) — at perceptual roughness 0.5,
// which is GGX alpha 0.25: a tight glossy lobe that shows a recognisable image
// of the sun and the sky. Every primitive created from the menu got that
// material (`SceneEditService::addNodeToScene`), and the owner's smoke of push
// #41 reported both halves of it: "a plane that darkens the whole scene" (a
// unit-albedo plane under the default sun renders near radiance 1 against a
// 0.117 sky, and the mean-of-logs exposure meter answers by darkening
// everything else) and "a sharp warped bright streak with a dark smear on a
// matte sphere" (which was not matte).
//
// AND A SECOND, SHARPER HAZARD. The project's reflection roughness cutoff is
// 0.40 and the ray arm's feather is 0.1, so the traced-reflection gate is
// `1 - smoothstep(0.30, 0.50, r)` and reaches exactly ZERO at r = 0.50. The old
// default sat ON that edge: the G-buffer packs the GGX alpha over [0.001, 1]
// and the shader decodes `sqrt(a * 0.999 + 0.001)`, so 0.25 comes back as
// 0.5007 and the default material was outside the band by seven ten-thousandths
// — one least-significant bit of the G-buffer, a normal map, or a roughness map
// reading 0.49 would have put a one-sample-per-pixel un-denoised trace on a
// surface the user calls matte. A DEFAULT THAT LANDS ON A TIER'S THRESHOLD IS A
// DEFECT CLASS, and this suite is the guard that it cannot be re-crossed
// silently.
//
// WHAT IS ASSERTED:
//   1. The constructor reads the one definition (iris::defaultmaterial).
//   2. The values are physical: albedo below 1.0 and in the range a neutral
//      surface occupies; roughness at or above the matte threshold; not a metal.
//   3. The default clears the reflection band by a real margin, computed from
//      the SHIPPED cutoff and feather rather than from a copy of them.
//   4. The renderer's fallback for a mesh node with NO material states the same
//      surface: the panel shows what the picture renders.
//
// Document only: no display, no renderer.
#include <QGuiApplication>
#include <cstdio>
#include <cmath>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/core/color.h"
#include "jahshaka/engine/Types.h"

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                     \
    } while (0)

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // ---- 1. the constructor reads the one definition --------------------
    auto m = iris::PbrMaterial::create();
    CHECK(m->baseColor == iris::defaultmaterial::baseColor(),
          "the constructor's base colour IS the one definition");
    CHECK(std::fabs(m->roughnessFactor - iris::defaultmaterial::kRoughness) < 1e-6f,
          "...and its roughness");
    CHECK(std::fabs(m->metallicFactor - iris::defaultmaterial::kMetalness) < 1e-6f,
          "...and its metalness");
    CHECK(std::fabs(m->baseColorFactor - 1.0f) < 1e-6f,
          "...with no hidden factor on top of the colour");

    // ---- 2. the values are physical -------------------------------------
    const iris::LinearColor lin = iris::linearOf(m->baseColor);
    std::printf("   albedo sRGB(%d,%d,%d) -> linear %.4f; roughness %.3f; metalness %.3f\n",
                m->baseColor.red(), m->baseColor.green(), m->baseColor.blue(),
                lin.r, m->roughnessFactor, m->metallicFactor);
    CHECK(lin.r < 0.90f, "NO DIFFUSE SURFACE REFLECTS MORE THAN FRESH SNOW");
    CHECK(lin.r > 0.15f, "...and an unauthored object is not a grey card either");
    CHECK(std::fabs(lin.r - lin.g) < 1e-4f && std::fabs(lin.g - lin.b) < 1e-4f,
          "...it is neutral");
    // Matte begins where the GGX lobe stops resolving an image of a light:
    // perceptual 0.75 is alpha 0.5625, and 0.5 was alpha 0.25 — a mirror-ish
    // satin surface that a user calling it "matte" is right to reject.
    CHECK(m->roughnessFactor >= 0.70f, "AND IT IS MATTE, not satin");
    CHECK(std::fabs(m->metallicFactor) < 1e-6f, "a metal is a decision, never a default");
    // The linear value the RENDERER's fallback carries has to be the same
    // number the panel's colour decodes to, or the two disagree again.
    CHECK(std::fabs(lin.r - iris::defaultmaterial::kBaseColorLinear) < 0.01f,
          "the linear constant the renderer reads matches the colour the panel shows");

    // ---- 3. it clears the reflection band -------------------------------
    // The band, from the SHIPPED numbers: the project's cutoff (percent) and
    // the engine's feather. Anything inside [cutoff - feather, cutoff + feather]
    // gets a partial traced reflection; the edge itself is where a
    // one-sample-per-pixel trace first appears.
    iris::Scene scene;
    const float cutoff = float(scene.reflectionRoughnessCutoff) / 100.0f;
    // THE SHIPPED FEATHER, read rather than copied (DRAG-1 round 2, F13): a
    // guard that carries its own copy of the number it guards against stops
    // guarding the day somebody moves the real one.
    const float feather = jahshaka::engine::kRayReflectFeather;
    const float bandTop = cutoff + feather;
    const float margin = m->roughnessFactor - bandTop;
    std::printf("   cutoff %.2f, feather %.2f -> the ray band ends at %.2f; the default is %.2f "
                "(margin %.3f)\n", cutoff, feather, bandTop, m->roughnessFactor, margin);
    CHECK(margin > 0.05f,
          "THE DEFAULT IS NOT ON THE REFLECTION BAND'S EDGE (it used to clear it by 0.0007)");

    std::printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
