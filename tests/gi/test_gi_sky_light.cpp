// THE SKY LIGHT — ambient is a light, and the sky is what it reads
// (SPECS/SKY_LIGHT_SPEC.md §6, owner decisions D14/D15 + picks 1-4).
//
// What this gates, in one sentence: a scene's ambient is the FIRST VISIBLE
// LightType::Sky node scaling the live sky's own cosine-convolved integral, and
// a scene without one has no ambient at all.
//
// It links the MIRROR as well as the engine, like gi.coalesce does, because the
// contract is the pair: the resolver and the SH scaling are mirror-side, the SH
// push, the VCT derivation and the sun disc are engine-side, and every one of
// the eight cases below is a statement about document -> mirror -> pixels.
//
// EVERY CASE IS A PIXEL OR A COEFFICIENT, never a flag: "the Sky Light is
// bound" is not an assertion anybody can trust — "the sphere's top is brighter
// than its bottom and its hue is the sky's" is.
#include <QGuiApplication>

#include "tests/support/testmesh.h"
#include <QImage>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>
#include <vector>

#include "irisgl/core/color.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

namespace {
float lum(const Colour &c) { return c.r + c.g + c.b; }
}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-sky-light-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("skylight", 256, 256, Colour(0, 0, 0));
    Scene *escene = engine->createScene("skylight");
    view->setScene(escene);

    // ---- the document ------------------------------------------------------
    // ONE matte white sphere, filling the middle of the frame, and NO other
    // light: everything the sphere shows is the skylight. White and fully rough
    // so the shading is the irradiance and nothing else — a tinted albedo would
    // make every hue assertion below a statement about the albedo instead.
    auto doc = iris::Scene::create();
    doc->giMode = iris::GiMode::OFF;
    doc->skyType = iris::SkyType::SINGLE_COLOR;
    doc->skyColor = QColor(150, 90, 45);
    doc->fogEnabled = false;
    doc->shadowEnabled = false;
    doc->hdrEnabled = false;       // the plain LDR readback: linear radiance, 8 bits

    auto sphere = iris::MeshNode::create();
    sphere->setName("sphere");
    sphere->setMesh(testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/sphere.obj")));
    sphere->setLocalPos(iris::Vec3(0.0f, 0.0f, 0.0f));
    // Unit scale: the sphere then spans about +/-32 px of a 256x256 frame at
    // this pose, which leaves the sky visible around it for the disc cases.
    sphere->setLocalScale(iris::Vec3(1.0f, 1.0f, 1.0f));
    auto mat = iris::PbrMaterial::create();
    mat->setBaseColor(QColor(255, 255, 255));
    mat->setRoughnessFactor(1.0f);
    mat->setMetallicFactor(0.0f);
    sphere->setMaterial(mat);
    doc->getRootNode()->addChild(sphere);

    auto skyLight = iris::LightNode::create();
    skyLight->setName("Sky Light");
    skyLight->lightType = iris::LightType::Sky;
    skyLight->intensity = 1.0f;
    skyLight->color = QColor(255, 255, 255);
    doc->getRootNode()->addChild(skyLight);

    SceneMirror mirror(escene);
    // NO EDITOR HELPERS. The light icons are billboards at the light's node
    // position, drawn in the overlay layer at full white — and both lights in
    // this scene sit at the origin, i.e. exactly on top of the probes and of
    // the sun disc. A measuring suite renders the SCENE.
    mirror.setLightWires(false);
    mirror.setSource(doc);
    auto cam = iris::CameraNode::create();
    cam->setLocalPos(iris::Vec3(0.0f, 0.0f, 6.0f));
    cam->lookAt(iris::Vec3(0.0f, 0.0f, 0.0f));
    cam->update(0.0f);
    mirror.applyCamera(cam, view);

    const auto frame = [&]() {
        doc->refresh();
        mirror.sync();
        mirror.applySky(view);
        mirror.applyEnvironment(view, engine.get());
        engine->renderOneFrame();
    };
    // Rows 108 and 148 are up the sphere's dome and down its underside, both on
    // the centre column and both safely inside its ~32 px silhouette.
    const auto shade = [&](int y) {
        Image img;
        view->readPixels(img);
        return img.at(128, y);
    };

    for (int f = 0; f < 4; ++f) frame();

    // ---- 1. THE SKY IS THE LIGHT --------------------------------------------
    // D14 in pixels: the sky is the only light in the scene, so a white sphere
    // shows the SKY'S colour (r > g > b for a 150/90/45 sky) and nothing else.
    //
    // NOT "the top is brighter than the bottom" for a colour sky, which is what
    // SKY_LIGHT_SPEC §6 case 1 asks for: a SINGLE_COLOR sky is UNIFORM, its
    // irradiance is SH band 0 alone (case 5 asserts exactly that), and a band-0
    // -only field is the same in every direction — a uniform sky physically
    // CANNOT light a top brighter than a bottom. The directional half of the
    // case belongs to a sky that has a direction in it, so it is asserted below
    // on a GRADIENT sky, which is the same code path through the same seam.
    {
        const Colour top = shade(108), bottom = shade(148);
        std::printf("   top    r=%.3f g=%.3f b=%.3f\n", top.r, top.g, top.b);
        std::printf("   bottom r=%.3f g=%.3f b=%.3f\n", bottom.r, bottom.g, bottom.b);
        CHECK(lum(top) > 0.1f, "1a. a colour sky lights the sphere at all");
        CHECK(top.r > top.g && top.g > top.b,
              "1b. in the SKY'S hue (r > g > b for a 150/90/45 sky) on a WHITE sphere");
        CHECK(std::fabs(lum(top) - lum(bottom)) < 0.02f,
              "1c. and evenly: a uniform sky has no direction, so neither has its light");
    }

    // ---- 1d. A SKY WITH A DIRECTION IN IT LIGHTS DIRECTIONALLY --------------
    // The other half of the same seam: a gradient sky, bright at the zenith and
    // black at the nadir, must light the sphere's dome and not its underside.
    // This is the assertion that says the SH really is the sky's shape and not
    // just its mean.
    {
        doc->skyType = iris::SkyType::GRADIENT;
        // The ramp's middle stop sits at `offset` from the BOTTOM and is clamped
        // to [0.01, 0.99] (iris::bakeGradientSky), so an offset of 0 makes the
        // whole sky the top colour with a one-percent band of the bottom one —
        // a uniform sky by another name. Half-way is the honest gradient.
        doc->gradientTop = QColor(200, 200, 200);
        doc->gradientMid = QColor(100, 100, 100);
        doc->gradientBot = QColor(0, 0, 0);
        doc->gradientOffset = 0.5f;
        for (int f = 0; f < 6; ++f) frame();
        const Colour top = shade(108), bottom = shade(148);
        std::printf("   gradient top %.3f bottom %.3f (sky above %.3f below %.3f)\n",
                    lum(top), lum(bottom), lum(shade(8)), lum(shade(248)));
        CHECK(lum(top) > lum(bottom) * 1.5f,
              "1d. a sky bright above and black below lights the dome and not the underside");
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        for (int f = 0; f < 6; ++f) frame();
    }

    // ---- 2. A BLACK SKY LIGHTS NOTHING --------------------------------------
    {
        doc->skyColor = QColor(0, 0, 0);
        for (int f = 0; f < 4; ++f) frame();
        const Colour top = shade(108), bottom = shade(148);
        CHECK(lum(top) < 0.02f && lum(bottom) < 0.02f,
              "2. a black sky lights nothing at all (both probes <= 2/255)");
        doc->skyColor = QColor(150, 90, 45);
        for (int f = 0; f < 4; ++f) frame();
    }

    // ---- 3. NO SKY LIGHT = NO AMBIENT ---------------------------------------
    // Both halves: HIDING one is the user-facing off switch, and REMOVING one
    // leaves the scene with nothing to light it. Neither leaves a residue.
    {
        const Colour lit = shade(108);
        skyLight->setVisible(false);
        for (int f = 0; f < 4; ++f) frame();
        const Colour hidden = shade(108);
        CHECK(lum(lit) > 0.05f && lum(hidden) < 0.02f,
              "3a. hiding the Sky Light puts the scene out");
        skyLight->setVisible(true);
        for (int f = 0; f < 4; ++f) frame();
        CHECK(lum(shade(108)) > 0.05f, "3b. showing it again brings the light back");

        doc->getRootNode()->removeChild(skyLight);
        for (int f = 0; f < 4; ++f) frame();
        CHECK(lum(shade(108)) < 0.02f, "3c. REMOVING it leaves the scene with no ambient at all");
        doc->getRootNode()->addChild(skyLight);
        for (int f = 0; f < 4; ++f) frame();
    }

    // ---- 4. INTENSITY IS LINEAR, AND THE TINT IS A PER-CHANNEL GAIN ---------
    {
        const Colour one = shade(108);
        skyLight->intensity = 2.0f;
        for (int f = 0; f < 4; ++f) frame();
        const Colour two = shade(108);
        const float ratio = lum(one) > 0.001f ? lum(two) / lum(one) : 0.0f;
        std::printf("   intensity 2.0 / 1.0 radiance ratio = %.3f\n", ratio);
        CHECK(ratio > 1.85f && ratio < 2.15f,
              "4a. doubling the Sky Light's intensity doubles the radiance it delivers");
        skyLight->intensity = 1.0f;
        skyLight->color = QColor(255, 0, 0);
        for (int f = 0; f < 4; ++f) frame();
        const Colour red = shade(108);
        CHECK(red.r > 0.02f && red.g < 0.01f && red.b < 0.01f,
              "4b. a pure red tint leaves only the red channel lit");
        skyLight->color = QColor(255, 255, 255);
        for (int f = 0; f < 4; ++f) frame();
    }

    // ---- 5. A UNIFORM SKY IS BAND 0, AND BAND 0 IS linear(colour) -----------
    // The colour-space rule at its source (§4): the strip a SINGLE_COLOR sky
    // bakes is uploaded sRGB and integrated through the same decode a painted
    // sky's texels take, so a uniform sky's irradiance is exactly the decoded
    // colour with every higher band at zero. This is the assertion that makes
    // "a picked colour and a painted colour are the same colour" true.
    // SINCE SKY-GPU the integral is the ENGINE's: it captures the sky it drew
    // into a cubemap and integrates that, so this reads Scene::skyAmbientSh
    // rather than a host-side function over a QImage. The assertion is the
    // same one and it is now end to end — picked colour, uploaded strip,
    // rendered sky, captured cube, integral — which is exactly the chain the
    // colour-space rule has to hold across. AT THE ORIGINAL TOLERANCES: the
    // capture is RGBA16_FLOAT, so the only error left is the cube's quadrature,
    // not an 8-bit sRGB step (which is ~5e-3 of linear radiance at mid-grey and
    // rounds differently on different hardware).
    {
        doc->skyColor = QColor(150, 90, 45);
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        for (int f = 0; f < 4; ++f) frame();
        float sh[27] = { 0.0f };
        CHECK(escene->skyAmbientSh(sh), "5a. the engine integrated the sky it drew");
        const iris::LinearColor want = iris::linearOf(QColor(150, 90, 45));
        std::printf("   band0 r=%.4f g=%.4f b=%.4f   linearOf r=%.4f g=%.4f b=%.4f\n",
                    sh[0], sh[1], sh[2], want.r, want.g, want.b);
        CHECK(std::fabs(sh[0] - want.r) < 2e-3f && std::fabs(sh[1] - want.g) < 2e-3f &&
                  std::fabs(sh[2] - want.b) < 2e-3f,
              "5b. band 0 of a uniform sky IS linearOf(the picked colour)");
        float worst = 0.0f;
        for (int i = 3; i < 27; ++i) worst = std::max(worst, std::fabs(sh[i]));
        std::printf("   worst higher band = %.5f\n", worst);
        CHECK(worst < 1e-3f, "5c. and every higher band is zero (a uniform sky has no direction)");
    }

    // ---- 6. THE SUN DISC IS WHERE THE LIGHT POINTS --------------------------
    // Drawn over EVERY sky type (owner pick 2) at the sun light's own angular
    // size, and switched by a WORLD setting (pick 4 / §193a). The camera looks
    // down -Z; a sun light travelling +Z puts the sun straight ahead, in the
    // middle of the frame — so the sphere is moved out of the way first.
    {
        sphere->setLocalPos(iris::Vec3(0.0f, -40.0f, 0.0f));   // right out of frame
        auto sun = iris::LightNode::create();
        sun->setName("Directional Light");
        sun->lightType = iris::LightType::Directional;
        sun->intensity = 1.0f;
        sun->shadowMap->shadowType = iris::ShadowMapType::None;
        // A document light travels down its local -Y. Pitching -90 about X turns
        // that onto +Z — the light travels AWAY from the camera, so the sun
        // itself is at -Z, dead ahead in the frame (measured, not assumed: the
        // opposite sign puts it behind the camera and the disc never appears).
        // The angular size is raised to 6 degrees so the disc is tens of pixels
        // across and the scan below is not measuring one texel. It is a WORLD
        // row (Scene::sunDiscSize) since lane SUN-DISC-1, not a light row.
        sun->setLocalRot(iris::Quat::fromEulerAngles(-90.0f, 0.0f, 0.0f));
        const float shippedDiscSize = doc->sunDiscSize;
        doc->sunDiscSize = 6.0f;
        doc->getRootNode()->addChild(sun);
        for (int f = 0; f < 4; ++f) frame();

        // WHERE IS THE BRIGHTEST PIXEL? A scan rather than a guessed coordinate:
        // the assertion is "the disc is where the light points and it MOVES with
        // it", and a scan says both without encoding the projection by hand.
        Image img;
        // The CENTROID of the brightest pixels, not the first one found: a disc
        // this bright saturates, so "the maximum" is a whole plateau and a scan
        // that stops at the first hit reports its top edge.
        const auto brightest = [&](int &bx, int &by) {
            view->readPixels(img);
            float best = -1.0f;
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x) best = std::max(best, lum(img.at(x, y)));
            double sx = 0.0, sy = 0.0; int n = 0;
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    if (lum(img.at(x, y)) >= best - 1e-4f) { sx += x; sy += y; ++n; }
            bx = n ? int(sx / n) : -1;
            by = n ? int(sy / n) : -1;
            return best;
        };
        int bx = 0, by = 0;
        const float peak = brightest(bx, by);
        const Colour offSun = img.at(4, 4);
        std::printf("   disc peak %.3f at (%d,%d); sky corner %.3f\n", peak, bx, by, lum(offSun));
        CHECK(peak > lum(offSun) + 0.25f,
              "6a. the sun disc is drawn over a COLOUR sky, brighter than the sky itself");
        CHECK(std::abs(bx - 128) <= 8 && std::abs(by - 128) <= 8,
              "6a2. and it lands where the sun light points (frame centre for a -Z sun)");

        // Pitch the light 25 degrees: the disc must travel with it.
        sun->setLocalRot(iris::Quat::fromEulerAngles(-65.0f, 0.0f, 0.0f));
        for (int f = 0; f < 4; ++f) frame();
        int mx = 0, my = 0;
        brightest(mx, my);
        std::printf("   after a 25 deg pitch the peak is at (%d,%d)\n", mx, my);
        CHECK(std::abs(my - by) > 30,
              "6b. rotating the sun light moves the disc with it");
        sun->setLocalRot(iris::Quat::fromEulerAngles(-90.0f, 0.0f, 0.0f));
        for (int f = 0; f < 4; ++f) frame();

        // The WORLD switch (owner §193a: "we need a World setting to hide it").
        doc->sunDiscVisible = false;
        for (int f = 0; f < 4; ++f) frame();
        int ox = 0, oy = 0;
        const float offPeak = brightest(ox, oy);
        std::printf("   disc off: brightest pixel now %.3f\n", offPeak);
        CHECK(std::fabs(offPeak - lum(offSun)) < 0.02f,
              "6c. world.sunDisc({visible:false}) removes it, leaving the bare sky");
        doc->sunDiscVisible = true;
        for (int f = 0; f < 4; ++f) frame();
        CHECK(brightest(ox, oy) > lum(offSun) + 0.25f, "6d. and switching it back restores it");

        // THE DISC SURVIVES A SKY THAT WENT AWAY AND CAME BACK. destroySky()
        // takes the disc with it (a NoSky description is a full clear), so the
        // engine has to FORGET what it last pushed — otherwise the next push is
        // value-equal to a disc that no longer exists and the sun stays missing
        // until something else moves it. (Round-2 review item 5.)
        {
            const auto skyWas = doc->skyType;
            doc->skyType = iris::SkyType::EQUIRECTANGULAR;   // no texture: NoSky
            doc->setSkyTexture(iris::Texture2DPtr());
            for (int f = 0; f < 4; ++f) frame();
            doc->skyType = skyWas;
            for (int f = 0; f < 6; ++f) frame();
            int rx = 0, ry = 0;
            const float back = brightest(rx, ry);
            std::printf("   after NoSky and back, brightest %.3f at (%d,%d)\n", back, rx, ry);
            CHECK(back > lum(offSun) + 0.25f,
                  "6g. the disc comes back after the sky went away and returned");
        }

        // A ZERO ANGULAR SIZE IS NOT A DISC (round-2 review item 6): the
        // shader's edge is a smoothstep between two equal numbers at radius 0.
        {
            const float was = doc->sunDiscSize;
            doc->sunDiscSize = 0.0f;   // the field directly: the dial clamps at 0.1
            for (int f = 0; f < 4; ++f) frame();
            int zx = 0, zy = 0;
            const float zero = brightest(zx, zy);
            CHECK(std::fabs(zero - lum(offSun)) < 0.02f,
                  "6h. a size of 0 draws no disc at all (not a full-screen flash)");
            doc->sunDiscSize = was;
            for (int f = 0; f < 4; ++f) frame();
        }

        // ---- 6i. THE DISC'S SIZE IS A WORLD DIAL THAT COSTS NO LIGHT ------
        // (Lane SUN-DISC-1; owner 2026-09-14: "the sun disc is too small — make
        // it about 4x larger".) Two statements, both in pixels:
        //
        //   the disc's DIAMETER on screen is proportional to the dial, and
        //   the ENERGY it puts in the frame does not depend on the dial at all.
        //
        // The second is the one that matters: radiance times solid angle is
        // irradiance, so a disc drawn four times wider at the same radiance
        // would put SIXTEEN times the energy into the bloom pass and into an
        // `inProbes` capture. The mirror divides the radiance by the solid
        // angle the dial asked for; this measures that it really did.
        {
            // AN UNCLIPPED DISC. The shipped disc saturates on purpose (it is
            // the sun), and a clipped pixel cannot be summed — so the sun's
            // intensity is dropped until the brightest pixel of the SMALLER
            // disc is still inside the 8-bit range, and put back afterwards.
            const float intensityWas = sun->intensity;
            const float sizeWas = doc->sunDiscSize;
            sun->intensity = 0.13f;
            const Colour sky = img.at(4, 4);

            // The disc's footprint and the light it adds over the bare sky.
            // Everything above the sky by a threshold that a uniform colour
            // sky cannot produce on its own (it is flat to the bit).
            struct Shot { int pixels; double excess; float peak; };
            const auto measure = [&](float sizeDeg) {
                doc->sunDiscSize = sizeDeg;
                for (int f = 0; f < 4; ++f) frame();
                view->readPixels(img);
                Shot sh { 0, 0.0, 0.0f };
                for (int y = 0; y < 256; ++y)
                    for (int x = 0; x < 256; ++x) {
                        const Colour c = img.at(x, y);
                        const float over = lum(c) - lum(sky);
                        if (over > 0.02f) { ++sh.pixels; sh.excess += double(over); }
                        // PER CHANNEL: an 8-bit readback clips each channel on
                        // its own, and a clipped channel is a lost measurement.
                        sh.peak = std::max(sh.peak, std::max(c.r, std::max(c.g, c.b)));
                    }
                return sh;
            };
            const Shot small = measure(3.0f);
            const Shot big   = measure(6.0f);
            const double diaRatio = small.pixels > 0
                ? std::sqrt(double(big.pixels) / double(small.pixels)) : 0.0;
            const double energyRatio = small.excess > 0.0 ? big.excess / small.excess : 0.0;
            std::printf("   disc 3 deg: %d px, sum %.1f, peak %.3f | 6 deg: %d px, sum %.1f, "
                        "peak %.3f | diameter x%.3f, energy x%.4f\n",
                        small.pixels, small.excess, small.peak,
                        big.pixels, big.excess, big.peak, diaRatio, energyRatio);
            CHECK(small.peak < 0.98f && big.peak < 0.98f,
                  "6i0. both discs are measured UNCLIPPED (a clipped sum means nothing)");
            CHECK(diaRatio > 1.85 && diaRatio < 2.15,
                  "6i. doubling the dial doubles the disc's DIAMETER on screen");
            CHECK(std::fabs(energyRatio - 1.0) < 0.02,
                  "6i2. ...and puts the SAME total light in the frame (within 2%)");

            // AND THE FOUR TIMES THE OWNER ASKED FOR, at the shipped values:
            // the physical sun against the default disc.
            const Shot physical = measure(iris::kPhysicalSunDiscSize);
            const Shot shipped  = measure(iris::kDefaultSunDiscSize);
            const double shippedRatio = physical.pixels > 0
                ? std::sqrt(double(shipped.pixels) / double(physical.pixels)) : 0.0;
            std::printf("   physical 0.53 deg: %d px | shipped %.2f deg: %d px | diameter x%.2f\n",
                        physical.pixels, double(iris::kDefaultSunDiscSize), shipped.pixels,
                        shippedRatio);
            // A 0.53-degree disc is FOUR PIXELS of a 256-pixel frame, so this
            // ratio is quantised to about a quarter of itself — it is evidence
            // that the shipped disc really is several times the real sun on
            // screen, not a measurement of the number 4 (6i above measures the
            // law, on discs big enough to count).
            CHECK(shippedRatio > 3.0 && shippedRatio < 6.0,
                  "6i3. the shipped disc is about FOUR TIMES the real sun's width on screen");

            sun->intensity = intensityWas;
            doc->sunDiscSize = sizeWas;
            for (int f = 0; f < 4; ++f) frame();
        }

        // ---- 6j. THE ENGINE IS TOLD THE SIZE, AND THE HOST NORMALISES -----
        // The value side of 6i, exactly rather than within a tolerance: the
        // description the mirror pushes carries the dial's degrees, and its
        // radiance scales as 1/size^2 around the default.
        {
            const float sizeWas = doc->sunDiscSize;
            doc->sunDiscSize = iris::kDefaultSunDiscSize;
            for (int f = 0; f < 2; ++f) frame();
            const SunDisc atDefault = escene->sky().sun;
            doc->sunDiscSize = 2.0f * iris::kDefaultSunDiscSize;
            for (int f = 0; f < 2; ++f) frame();
            const SunDisc atDouble = escene->sky().sun;
            std::printf("   pushed: %.3f deg at radiance %.4f -> %.3f deg at %.4f\n",
                        atDefault.angularDiameterDeg, atDefault.colour.r,
                        atDouble.angularDiameterDeg, atDouble.colour.r);
            CHECK(std::fabs(atDefault.angularDiameterDeg - iris::kDefaultSunDiscSize) < 1e-4f &&
                      std::fabs(atDouble.angularDiameterDeg
                                - 2.0f * iris::kDefaultSunDiscSize) < 1e-4f,
                  "6j. the World row's degrees reach the engine unchanged");
            CHECK(atDefault.colour.r > 1e-4f &&
                      std::fabs(atDouble.colour.r * 4.0f - atDefault.colour.r)
                          < 1e-3f * atDefault.colour.r,
                  "6j2. ...and twice the size arrives at a QUARTER of the radiance");
            doc->sunDiscSize = sizeWas;
            for (int f = 0; f < 2; ++f) frame();
        }

        // THE DISC AND THE PROBES (owner pick 4, both options). `inProbes`
        // decides whether the disc carries kVisibleBit beside its own channel,
        // and kVisibleBit is exactly what the probe-capture passes ask for.
        CHECK(!doc->sunDiscInProbes, "6e. the disc is OUT of probe captures by default");
        doc->sunDiscInProbes = true;
        for (int f = 0; f < 4; ++f) frame();
        view->readPixels(img);
        CHECK(brightest(ox, oy) > lum(offSun) + 0.25f,
              "6f. including it in probes leaves the VIEW's picture unchanged");
        doc->sunDiscInProbes = false;

        // ---- 7. THE SUN DRIVES THE REALISTIC SKY ---------------------------
        // D15 inverted: the analytic sky's sun position comes from this light.
        // Rotating the light must move the sky's own brightest region.
        doc->skyType = iris::SkyType::REALISTIC;
        doc->sunDiscVisible = false;   // measure the SKY, not the disc on top of it
        // THE BAKE IS DEBOUNCED at 150 ms (applySky): a suite that only spins
        // frames re-bakes once and then measures the FIRST sun twice. Settling
        // means letting the debounce expire, not counting frames.
        const auto settle = [&]() {
            for (int f = 0; f < 4; ++f) frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            for (int f = 0; f < 4; ++f) frame();
        };
        // Sun ahead (at -Z, in frame) vs sun behind: the analytic sky's brightest
        // region is around the sun, so the two bakes must differ where the
        // camera is looking. A pixel probe, not a flag — nothing else in this
        // scene can move that number.
        sun->setLocalRot(iris::Quat::fromEulerAngles(-70.0f, 0.0f, 0.0f));
        settle();
        view->readPixels(img);
        const Colour aheadBright = img.at(128, 128);
        sun->setLocalRot(iris::Quat::fromEulerAngles(-70.0f, 180.0f, 0.0f));
        settle();
        view->readPixels(img);
        const Colour aheadDim = img.at(128, 128);
        std::printf("   realistic sky ahead: sun toward -Z %.3f, sun turned 180 deg %.3f\n",
                    lum(aheadBright), lum(aheadDim));
        CHECK(lum(aheadBright) > lum(aheadDim) * 1.2f,
              "7. the realistic sky's bake follows the SUN LIGHT's rotation (D15)");

        // ---- 7b. THE HORIZON CROSSING IS CONTINUOUS ------------------------
        // (Lane SUN-DISC-1, from the rig's smoke capture of 2026-09-14: between
        // sun pitches 86 and 90 the picture jumped in one step from bright and
        // warm to a deep blue sky with red-lit objects, and the sun's shadow
        // pass vanished in the same frame.)
        //
        // WHAT WAS ACTUALLY WRONG, and it is not the reddening. The scattering
        // model has NO ANSWER BELOW THE HORIZON — its time-of-day input clamps
        // at zero — so the tint FROZE at its horizon value and stayed there for
        // every elevation down to -90: a sun that had set went on lighting the
        // scene, drawing its disc and casting its shadow at a constant fraction
        // of noon, and the only thing that stopped it was a magic threshold
        // (kSunNightTint) deciding when night began. On the shipped sky that
        // frozen value is 0.0006 of noon, which is invisible and made the
        // threshold look harmless; dial the sky's density down to 0.1 and it is
        // 0.20 of noon — a fifth of the noon sun arriving from below the
        // ground. OgreScene::atmosphereSunTint now multiplies the model by the
        // EARTH: a smoothstep from 1 to 0 across the sun's own disc setting
        // through the refracted horizon (geometric elevation -0.305 to -0.835).
        //
        // So the assertion is not "no step" — a real sunset IS steep, the last
        // degree of elevation is where the air mass runs away — but "the three
        // things the sun does (light, disc, shadow) ride ONE number, that
        // number reaches zero continuously, and nothing switches off while the
        // scene can still see it".
        {
            doc->skyType = iris::SkyType::REALISTIC;
            struct Step { float pitch; float elevDeg; float tintMax; float mean; bool disc; };
            // THE SWEEP, TWICE. Once with the sun's own picture in the frame
            // (its disc, and a sphere it lights) and once with neither — so the
            // difference between the two runs IS the sun's contribution to the
            // picture, separated from the SKY's, which is drawn by the
            // atmosphere model itself and is not this lane's to move.
            const auto sweepOver = [&](bool withSun) {
                doc->sunDiscVisible = withSun;
                // The lit object, OFF CENTRE when it is in: the disc is drawn
                // where the sun points, which is the middle of this frame, and
                // a sphere at the origin would stand in front of it.
                sphere->setLocalPos(withSun ? iris::Vec3(-1.5f, 0.0f, 0.0f)
                                            : iris::Vec3(0.0f, -40.0f, 0.0f));
                std::vector<Step> out;
                for (int p = -84; p >= -92; --p) {
                    sun->setLocalRot(iris::Quat::fromEulerAngles(float(p), 0.0f, 0.0f));
                    settle();
                    view->readPixels(img);
                    double sum = 0.0;
                    for (int y = 0; y < 256; ++y)
                        for (int x = 0; x < 256; ++x) sum += double(lum(img.at(x, y)));
                    const iris::Vec3 toSun = -sun->getLightDir().normalized();
                    const Colour tint = escene->atmosphereSunTint(
                        Vec3(toSun.x(), toSun.y(), toSun.z()));
                    Step st;
                    st.pitch = float(p);
                    st.elevDeg = float(std::asin(std::max(-1.0f, std::min(1.0f, toSun.y())))
                                       * 180.0 / M_PI);
                    st.tintMax = std::max(tint.r, std::max(tint.g, tint.b));
                    st.mean = float(sum / (256.0 * 256.0));
                    st.disc = escene->sky().sun.enabled;
                    out.push_back(st);
                }
                return out;
            };
            const std::vector<Step> sweep = sweepOver(true);
            const std::vector<Step> bare  = sweepOver(false);
            for (size_t i = 0; i < sweep.size(); ++i)
                std::printf("   pitch %.0f (elev %+5.2f deg): tint %.6f  frame mean %.4f  "
                            "sky alone %.4f  the sun's picture %+.4f  disc %s\n",
                            sweep[i].pitch, sweep[i].elevDeg, sweep[i].tintMax, sweep[i].mean,
                            bare[i].mean, sweep[i].mean - bare[i].mean,
                            sweep[i].disc ? "on" : "off");
            // (a) THE SUN'S OWN CONTRIBUTION IS MONOTONE. One number drives the
            // light, the disc and the shadow, and it only ever falls as the sun
            // goes down — no step back up, no plateau, and it ENDS at zero.
            bool monotone = true;
            for (size_t i = 1; i < sweep.size(); ++i)
                if (sweep[i].tintMax > sweep[i - 1].tintMax + 1e-7f) monotone = false;
            CHECK(monotone, "7b. the sun's tint falls monotonically through the horizon");
            CHECK(sweep.back().tintMax == 0.0f,
                  "7b2. ...and REACHES ZERO below it (the model's frozen plateau is gone)");
            // (b) NOTHING SWITCHES OFF WHILE IT IS STILL VISIBLE. The frame the
            // disc (and with it the sun's shadow) leaves is a frame in which
            // the sun's own light is already under one 8-bit step of noon.
            float tintWhenDiscLeft = -1.0f;
            for (size_t i = 1; i < sweep.size(); ++i)
                if (sweep[i - 1].disc && !sweep[i].disc) tintWhenDiscLeft = sweep[i].tintMax;
            std::printf("   the disc left at tint %.6f (one 8-bit step of noon = %.6f)\n",
                        tintWhenDiscLeft, 1.0f / 255.0f);
            CHECK(tintWhenDiscLeft >= 0.0f && tintWhenDiscLeft < 1.0f / 255.0f,
                  "7b3. the disc and the sun's shadow leave only once the sun is invisible");
            // (c) NO SINGLE-STEP JUMP IN WHAT THE SUN PUTS IN THE PICTURE.
            // "The sun's picture" is the frame mean MINUS the same frame with
            // no disc and nothing for the sun to light. Its absolute value is
            // negative here and means nothing (a dark sphere in front of a
            // bright sky lowers the mean); what is measured is how much it
            // MOVES from one degree to the next, which is exactly the disc
            // appearing or disappearing and the light on the sphere changing,
            // with the sky divided out. 5% of the frame mean is the bound: the
            // whole of the sun's picture here is about 3% of the frame, so a
            // hard flip that took the disc and the sun's light out in one step
            // could not pass it, and 0.7% is what a continuous fade measures.
            //
            // THE FRAME AS A WHOLE IS NOT BOUNDED HERE, and the number printed
            // above says why: the SKY's own single-degree step at the crossing
            // is ~48%. That is Ogre's AtmosphereNpr, not ours — its
            // `lightDensity = densityCoeff / max(sunHeight, 0.0035)^0.75` with
            // `sunHeight = sin(normalizedTimeOfDay * PI)` and a time-of-day
            // clamped at zero gives the model no twilight at all: the sky
            // collapses inside the last degree of elevation and then stays at
            // that value all night. Reported as an upstream finding rather than
            // patched under a lane about the disc's size.
            float worstSun = 0.0f, worstAt = 0.0f, worstSky = 0.0f;
            for (size_t i = 1; i < sweep.size(); ++i) {
                const float shareNow  = sweep[i].mean - bare[i].mean;
                const float sharePrev = sweep[i - 1].mean - bare[i - 1].mean;
                const float step = std::fabs(shareNow - sharePrev)
                                   / std::max(1e-4f, sweep[i - 1].mean);
                if (step > worstSun) { worstSun = step; worstAt = sweep[i].pitch; }
                worstSky = std::max(worstSky, std::fabs(bare[i].mean - bare[i - 1].mean)
                                                  / std::max(1e-4f, bare[i - 1].mean));
            }
            std::printf("   largest single-degree step: the SUN's share %.1f%% (at pitch %.0f), "
                        "the SKY alone %.1f%%\n",
                        worstSun * 100.0f, worstAt, worstSky * 100.0f);
            CHECK(worstSun < 0.05f,
                  "7b4. one degree of sun never moves the sun's share of the picture by 5%");

            // (d) THE THIN-SKY CASE, which is what the frozen plateau really
            // cost: with the density dialled down, a sun 30 degrees UNDER the
            // ground used to light the scene at a fifth of noon for ever.
            {
                const float densityWas = doc->skyRealistic.density;
                doc->sunDiscVisible = true;   // the second sweep left it off
                doc->skyRealistic.density = 0.1f;
                sun->setLocalRot(iris::Quat::fromEulerAngles(-120.0f, 0.0f, 0.0f));  // 30 deg under
                settle();
                const iris::Vec3 toSun = -sun->getLightDir().normalized();
                const Colour tint = escene->atmosphereSunTint(
                    Vec3(toSun.x(), toSun.y(), toSun.z()));
                std::printf("   thin sky (density 0.1), sun 30 deg BELOW: tint %.4f %.4f %.4f, "
                            "disc %s\n", tint.r, tint.g, tint.b,
                            escene->sky().sun.enabled ? "on" : "off");
                CHECK(tint.r == 0.0f && tint.g == 0.0f && tint.b == 0.0f,
                      "7b5. a set sun lights nothing, at any sky density");
                CHECK(!escene->sky().sun.enabled,
                      "7b6. ...and draws no disc under the ground");
                doc->skyRealistic.density = densityWas;
            }
            sun->setLocalRot(iris::Quat::fromEulerAngles(-70.0f, 0.0f, 0.0f));
            sphere->setLocalPos(iris::Vec3(0.0f, -40.0f, 0.0f));
            settle();
        }

        doc->sunDiscVisible = true;
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        doc->getRootNode()->removeChild(sun);
        sphere->setLocalPos(iris::Vec3(0.0f, 0.0f, 0.0f));
        for (int f = 0; f < 4; ++f) frame();
    }

    // ---- 8. A FLAT COLOUR EQUALS THE SAME COLOUR PAINTED IN A TEXTURE -------
    // The colour-space rule, end to end and in PIXELS (§4): the SINGLE_COLOR
    // sky's strip and an EQUIRECT sky painted the same colour must light the
    // sphere identically. Before this lane the first was raw and the second
    // decoded, and the two disagreed by 2.33x.
    {
        const Colour flat = shade(108);
        // The gradient sky with three identical stops IS "the same colour
        // painted into a texture": a real image, through the image path.
        doc->skyType = iris::SkyType::GRADIENT;
        doc->gradientTop = QColor(150, 90, 45);
        doc->gradientMid = QColor(150, 90, 45);
        doc->gradientBot = QColor(150, 90, 45);
        for (int f = 0; f < 6; ++f) frame();
        const Colour painted = shade(108);
        std::printf("   flat   r=%.4f g=%.4f b=%.4f\n", flat.r, flat.g, flat.b);
        std::printf("   painted r=%.4f g=%.4f b=%.4f\n", painted.r, painted.g, painted.b);
        const float tol = 2.0f / 255.0f;
        CHECK(std::fabs(flat.r - painted.r) < tol && std::fabs(flat.g - painted.g) < tol &&
                  std::fabs(flat.b - painted.b) < tol,
              "8. a picked colour and the same colour painted into a sky texture agree (<= 2/255)");
        doc->skyType = iris::SkyType::SINGLE_COLOR;
        for (int f = 0; f < 4; ++f) frame();
    }

    // ---- 9. TWO SKY LIGHTS: the FIRST is the skylight -----------------------
    // The resolver half of the `sky.duplicate` scene issue (the ISSUE itself is
    // Studio-side and gated by ui.scene_issues; what THIS suite owns is that a
    // second Sky Light changes no pixel, which is why the issue has to exist).
    {
        const Colour one = shade(108);
        auto second = iris::LightNode::create();
        second->setName("Sky Light 2");
        second->lightType = iris::LightType::Sky;
        second->intensity = 8.0f;
        second->color = QColor(0, 0, 255);
        doc->getRootNode()->addChild(second);
        for (int f = 0; f < 4; ++f) frame();
        const Colour two = shade(108);
        CHECK(std::fabs(lum(one) - lum(two)) < 0.01f && two.b < two.r,
              "9a. a SECOND Sky Light changes nothing: the first one is the skylight");
        CHECK(doc->skyLights().size() == 2 && doc->skyLight() == skyLight,
              "9b. skyLights() sees both and skyLight() resolves to the first");
        // Hiding the FIRST hands the role to the second — which is what makes
        // "hide one" a real fix for the duplicate issue.
        skyLight->setVisible(false);
        for (int f = 0; f < 4; ++f) frame();
        const Colour blue = shade(108);
        CHECK(doc->skyLight() == second && blue.b > blue.r,
              "9c. hiding the first hands the role (and the tint) to the second");
        skyLight->setVisible(true);
        doc->getRootNode()->removeChild(second);
        for (int f = 0; f < 4; ++f) frame();
    }

    // ---- 10. THE VCT PAIR IS THE SKY LIGHT'S SH ----------------------------
    // The Photon seam, pinned rather than assumed (spec §8.3): with VCT bound
    // the shader's ambient comes from VctLighting's hemisphere pair, and ENGINE-4
    // derives that pair from the pushed SH (c0 +/- c1). Nothing else may write it,
    // so switching the Sky Light off must put the VCT ambient out too.
    {
        doc->giMode = iris::GiMode::VCT;
        doc->giQuality = iris::GiQuality::LOW;
        // The volume is the renderer's automatic fit (owner decision D8).
        for (int f = 0; f < 10; ++f) frame();
        const Colour lit = shade(108);
        skyLight->intensity = 0.0f;
        for (int f = 0; f < 10; ++f) frame();
        const Colour dark = shade(108);
        std::printf("   VCT bound: skylight 1.0 -> %.3f, 0.0 -> %.3f\n", lum(lit), lum(dark));
        CHECK(lum(lit) > 0.05f && lum(dark) < lum(lit) * 0.25f,
              "10. with VCT bound the ambient is still the Sky Light's and nothing else's");
        skyLight->intensity = 1.0f;
    }

    // ---- 11. A SKY LIGHT EDIT COSTS NO GI RE-SOLVE -------------------------
    // (Audit A F3, fixed in PHOTON E0.) The mirror hashes every document light
    // into the VCT light signature, and that signature arms the settle: a
    // re-solve is a teardown, a re-voxelisation of the whole volume — under
    // Photon's cascades, of the whole chain — and every probe re-captured
    // twice. The Sky Light's only dials are colour, intensity and visibility,
    // all three of which already reach the renderer the cheap way (the ambient
    // SH into every voxel volume, plus one probe-grid stale with its own
    // reason), so hashing it bought a full re-solve for a change that was
    // already applied. It is never an Ogre::Light at all.
    {
        for (int f = 0; f < 20; ++f) frame();      // let anything already owed settle
        const quint64 refreshes = mirror.giRefreshCount();
        const unsigned long long rebuilds = escene->giStatus().rebuilds;
        const Colour before = shade(108);
        skyLight->intensity = 0.3f;
        // WELL PAST THE SETTLE WINDOW (15 stable frames / 250 ms): before the
        // fix this is exactly where the re-solve landed.
        for (int f = 0; f < 40; ++f) frame();
        const Colour dim = shade(108);
        std::printf("   sky light 1.0 -> 0.3: %.3f -> %.3f, re-solves %llu, rebuilds %llu\n",
                    lum(before), lum(dim),
                    (unsigned long long)(mirror.giRefreshCount() - refreshes),
                    escene->giStatus().rebuilds - rebuilds);
        CHECK(lum(dim) < lum(before) * 0.6f, "11a. the intensity edit reaches the picture");
        CHECK(mirror.giRefreshCount() == refreshes,
              "11b. ...and costs NO full GI re-solve (it is not a voxel light)");
        CHECK(escene->giStatus().rebuilds == rebuilds,
              "11c. ...and no from-scratch rebuild of the arm");
        skyLight->intensity = 1.0f;
        for (int f = 0; f < 25; ++f) frame();
        CHECK(mirror.giRefreshCount() == refreshes && escene->giStatus().rebuilds == rebuilds,
              "11d. and neither does putting it back");
    }

    // ---- 12. THE SKY REFLECTS AT THE SKY LIGHT'S GAIN, AND ONLY THEN -------
    // The owner's report, 2026-09-14: "with all lights off the GPU sky still
    // lights the scene". Their hypothesis was right and it was the SPECULAR
    // half. Case 3 above proves the diffuse half is gated — hide the Sky Light
    // and a matte sphere goes black — but the sky's own captured cube reached
    // every material through reflectionTexFor with no gate at all, so a MIRROR
    // went on reflecting the sky byte-identically with every light in the scene
    // hidden. It is the same light or it is not a light: a sky with no Sky
    // Light is a BACKDROP, still drawn, still visible behind the scene, and it
    // reflects nothing into it.
    //
    // The fixture turns the sphere into a mirror for this case — metal,
    // roughness 0 — so the pixel at its centre IS the environment sample and
    // nothing else. The sky stays on throughout: every assertion here is about
    // the LIGHT, and the last one proves the sky itself never went away.
    {
        mat->setMetallicFactor(1.0f);
        mat->setRoughnessFactor(0.02f);
        sphere->setMaterial(mat);
        skyLight->intensity = 1.0f;
        skyLight->color = QColor(255, 255, 255);
        for (int f = 0; f < 8; ++f) frame();
        const Colour mirrorOn = shade(128);          // dead centre: the mirror
        const Colour skyPixel = shade(8);            // top of the frame: the sky itself

        skyLight->setVisible(false);
        for (int f = 0; f < 8; ++f) frame();
        const Colour mirrorOff = shade(128);
        const Colour skyOff    = shade(8);
        std::printf("   mirror sphere: skylight on %.4f, hidden %.4f; sky backdrop %.4f -> %.4f\n",
                    lum(mirrorOn), lum(mirrorOff), lum(skyPixel), lum(skyOff));
        CHECK(lum(mirrorOn) > 0.05f, "12a. the mirror reflects the sky while the Sky Light is lit");
        CHECK(lum(mirrorOff) < lum(mirrorOn) * 0.05f,
              "12b. HIDING the Sky Light takes the sky's REFLECTION with it");
        CHECK(lum(skyOff) > lum(skyPixel) * 0.95f && lum(skyOff) < lum(skyPixel) * 1.05f,
              "12c. ...and the sky itself is untouched: it is a backdrop, not a light");

        // And it SCALES, linearly, like the diffuse half does in case 4.
        skyLight->setVisible(true);
        skyLight->intensity = 0.5f;
        for (int f = 0; f < 8; ++f) frame();
        const Colour half = shade(128);
        skyLight->intensity = 2.0f;
        for (int f = 0; f < 8; ++f) frame();
        const Colour twice = shade(128);
        const float rHalf  = lum(mirrorOn) > 0.001f ? lum(half) / lum(mirrorOn) : 0.0f;
        const float rTwice = lum(mirrorOn) > 0.001f ? lum(twice) / lum(mirrorOn) : 0.0f;
        std::printf("   mirror sphere gain: 0.5 -> %.3fx, 2.0 -> %.3fx of intensity 1.0\n",
                    rHalf, rTwice);
        CHECK(rHalf > 0.42f && rHalf < 0.58f,
              "12d. half the Sky Light is half the reflection");
        // The upper end is a ratio of TONEMAP-FREE 8-bit radiance, so it clips
        // where the sky is bright; assert the direction and a floor rather than
        // a two, which the format cannot carry for a bright sky.
        CHECK(rTwice > 1.3f, "12e. twice the Sky Light is a brighter reflection");

        skyLight->intensity = 1.0f;
        mat->setMetallicFactor(0.0f);
        mat->setRoughnessFactor(1.0f);
        sphere->setMaterial(mat);
        for (int f = 0; f < 8; ++f) frame();
    }

    mirror.setSource(iris::ScenePtr());
    std::printf(failures == 0 ? "gi.sky_light: PASS\n" : "gi.sky_light: %d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
