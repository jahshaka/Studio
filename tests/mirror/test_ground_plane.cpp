// mirror.ground_plane — THE GROUND PLANE WIDGET (WORLD-MODEL-1, owner
// 2026-09-30: the hidden built-in ground and its painted horizon were "a hack";
// every floor is an ordinary node now, and the infinite ground survives only as
// an EDITOR WIDGET toggled in View Options).
//
// WHAT THE MIRROR DOES. SceneMirror::setGroundPlane draws ONE mirror-owned 4 km
// quad, in a material the host hands it, just under y = 0 — no document node,
// and a BACKDROP in the engine's sense: every view draws it and no capture sees
// it (no shadow caster fit, no GI geometry, no probe face, no Atom id pass).
//
// THE CLAIMS:
//   A. OFF BY DEFAULT: a scene with no floor shows sky past the horizon line,
//      and the mirror owns no plane node;
//   B. ON: the ground reaches the horizon, and the plane's node is a BACKDROP
//      (Scene::nodeBackdrop — and so a helper: out of every capture pass) and,
//      where the renderer's split is live, NOT on Atom (it counts under
//      AtomDrawStatus::notWorld, and atomItems does not move);
//   C. A HIDE IS NOT A DELETE (DRAG-1): off keeps the quad and its material —
//      the engine's object census does not fall — and on again is the ground in
//      the VERY NEXT FRAME with the census exactly where it was;
//   D. THE PLAYER'S FLOOR HIDE lands on the next sync in a scene the verifier
//      cannot cover in one pass (PLAYER-FLOOR-1), on a node carrying
//      `defaultFloor` — the term the Player's setting is about.
//   E. IT RECEIVES SHADOWS AND CASTS NONE: a caster above darkens it (paired
//      against the same caster with casting off), and a box under it reads the
//      same with the plane on as off.
#include <QGuiApplication>

#include "../support/testmesh.h"
#include <QColor>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <utility>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace { constexpr unsigned kSize = 256; }

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_ground_plane-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // A sky-blue background, so "there is no ground there" is unmistakable.
    View *view = engine->createOffscreenView("groundplane", kSize, kSize, Colour(0.0f, 0.2f, 0.8f));
    Scene *target = engine->createScene("groundplane");
    if (!view || !target) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(target);
    target->setAmbient(Colour(0.5f, 0.5f, 0.5f), Colour(0.4f, 0.4f, 0.4f));

    auto doc = iris::Scene::create();
    auto grey = iris::PbrMaterial::create();
    grey->setBaseColor(QColor(200, 200, 200));

    auto sun = iris::LightNode::create();
    sun->intensity = 1.2f;
    sun->setLocalRot(iris::Quat::fromEulerAngles(-45.0f, 20.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    // A camera low and level, looking at the horizon: the band just below the
    // skyline is MANY HUNDREDS of metres away, so only an infinite ground can
    // fill it — and a scene with no floor at all has nothing else to.
    auto cam = iris::CameraNode::create();
    cam->angle = 45.0f; cam->nearClip = 0.1f; cam->farClip = 4000.0f;
    cam->setAspectRatio(1.0f);
    cam->setLocalPos(iris::Vec3(0.0f, 1.6f, 0.0f));
    cam->lookAt(iris::Vec3(0.0f, 1.55f, -100.0f));
    doc->getRootNode()->addChild(cam);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    // The host's material: a plain matte grey here (Studio hands it the
    // default floor material — the mirror does not care which).
    auto planeMaterial = iris::PbrMaterial::create();
    planeMaterial->setBaseColor(QColor(180, 180, 180));
    planeMaterial->setValue("roughness", 1.0f);
    mirror.setGroundPlaneMaterial(planeMaterial);
    mirror.sync();
    mirror.applyCamera(cam, view);

    Image img;
    const auto renderN = [&](int n) {
        for (int i = 0; i < n; ++i) { mirror.sync(); engine->renderOneFrame(); }
        view->readPixels(img);
    };
    const auto isSky = [](const Colour &c) { return c.b > c.r * 1.8f && c.b > 0.25f; };
    // The rows just below the skyline: the farthest GROUND pixels in the frame.
    const auto groundBandIsGround = [&]() {
        int ground = 0, total = 0;
        for (unsigned y = kSize / 2 + 1; y < kSize / 2 + 9; ++y)
            for (unsigned x = kSize / 4; x < 3 * kSize / 4; ++x) {
                ++total;
                if (!isSky(img.at(x, y))) ++ground;
            }
        return total ? double(ground) / double(total) : 0.0;
    };
    const auto census = [&]() {
        ObjectCounts c;
        engine->objectCounts(c);
        return c;
    };

    // ---- A. off by default -------------------------------------------------
    renderN(6);
    const double offFrac = groundBandIsGround();
    std::printf("   off: %.1f%% of the far band is ground\n", 100.0 * offFrac);
    CHECK(!mirror.groundPlane(), "A: the widget is OFF by default");
    CHECK(offFrac < 0.05, "A: with no floor and the widget off, the far band is sky");
    CHECK(mirror.groundPlaneNode() == 0, "A: ...and the mirror has built no plane at all");
    const AtomDrawStatus atomOff = target->atomDrawStatus();

    // ---- B. on: an infinite ground, and a backdrop ---------------------------
    mirror.setGroundPlane(true);
    renderN(6);
    const double onFrac = groundBandIsGround();
    const ObjectCounts before = census();
    std::printf("   on: %.1f%% of the far band is ground; census meshes %u materials %u nodes %u\n",
                100.0 * onFrac, before.meshes, before.materials, before.nodes);
    CHECK(onFrac > 0.95, "B: with the widget on, the ground reaches the horizon");
    const NodeId plane = mirror.groundPlaneNode();
    CHECK(plane != 0, "B: the plane is a mirror-owned engine node");
    CHECK(plane && target->nodeBackdrop(plane),
          "B: ...a BACKDROP: drawn by every view, out of every capture — no shadow-map caster, "
          "no reflection-probe face, no planar mirror, no GI geometry");
    CHECK(plane && target->nodeHelper(plane),
          "B: ...which implies the helper exclusion (the capture passes ask for kVisibleBit)");
    CHECK(doc->getRootNode()->children().size() == 2,
          "B: and the document never heard of it (the sun and the camera, nothing else)");
    const AtomDrawStatus atomOn = target->atomDrawStatus();
    std::printf("   atom: live %d on %d | off: atomItems %u notWorld %u | on: atomItems %u notWorld %u\n",
                int(atomOn.live), int(atomOn.on), atomOff.atomItems, atomOff.notWorld,
                atomOn.atomItems, atomOn.notWorld);
    if (atomOn.live) {
        CHECK(atomOn.notWorld == atomOff.notWorld + 1u,
              "B: NOT ON ATOM — the renderer's split counts it under notWorld (a backdrop: the id "
              "pass draws the world channels only)");
        CHECK(atomOn.atomItems == atomOff.atomItems, "B: ...and atomItems does not move");
    } else {
        std::printf("note: no GPU scene in this engine — the Atom route is read by scripting.e2e."
                    "default_ground instead\n");
    }

    // ---- C. a HIDE is not a DELETE -------------------------------------------
    mirror.setGroundPlane(false);
    renderN(6);
    const ObjectCounts hidden = census();
    const double hiddenFrac = groundBandIsGround();
    std::printf("   off again: %.1f%% ground; census meshes %u materials %u nodes %u\n",
                100.0 * hiddenFrac, hidden.meshes, hidden.materials, hidden.nodes);
    CHECK(hiddenFrac < 0.05, "C: off again, nothing is drawn");
    CHECK(hidden.meshes >= before.meshes && hidden.materials >= before.materials,
          "C: A HIDE DESTROYS NOTHING — the quad and its material are still there");
    mirror.setGroundPlane(true);
    mirror.sync();
    engine->renderOneFrame();
    view->readPixels(img);
    const double firstFrameFrac = groundBandIsGround();
    const ObjectCounts shown = census();
    CHECK(firstFrameFrac > 0.95, "C: ON AGAIN IN THE VERY NEXT FRAME (no rebuild)");
    CHECK(shown.meshes == before.meshes && shown.materials == before.materials,
          "C: ...and the census is exactly where it was");

    // ---- E. IT RECEIVES SHADOWS AND CASTS NONE ---------------------------------
    // The backdrop bit takes the plane out of every CASTER and capture pass; its
    // material (the default floor material in Studio) still samples the shadow
    // map in the main pass — which is what gives the Assets preview its contact
    // shadow. Both halves, paired in this process:
    //   E1 a RED cube above the plane darkens grey plane pixels, and the SAME
    //      scene with the cube's casting switched off darkens none (the cube's own
    //      red pixels are excluded by colour, so only the shadow is counted);
    //   E2 a grey box UNDER the plane, seen from below it (the plane faces up
    //      and is culled from beneath), reads the same with the plane on as off:
    //      the plane throws no shadow on it.
    {
        doc->shadowEnabled = true;
        view->setShadows(true);
        // A sun from ABOVE for this case (a light emits down its local -Y,
        // tipped 25 degrees): the fixture's own sun lights the far band and
        // barely reaches a floor seen from above, so a shadow would have nothing
        // to take away. Not so bright that the box under the plane (E2) clips.
        sun->setVisible(false);
        auto topSun = iris::LightNode::create();
        topSun->setLightType(iris::LightType::Directional);
        topSun->intensity = 0.8f;
        topSun->setLocalRot(iris::Quat::fromEulerAngles(-25.0f, 0.0f, 0.0f));
        doc->getRootNode()->addChild(topSun);
        auto red = iris::PbrMaterial::create();
        red->setBaseColor(QColor(220, 10, 10));
        auto caster = iris::MeshNode::create();
        caster->setName("Caster");
        caster->setMesh(testmesh::load(":/assets/models/cube.obj"));
        caster->setMaterial(red);
        caster->setLocalPos(iris::Vec3(0.0f, 1.5f, -8.0f));
        doc->getRootNode()->addChild(caster);
        cam->setLocalPos(iris::Vec3(0.0f, 14.0f, 4.0f));
        cam->lookAt(iris::Vec3(0.0f, 0.0f, -8.0f));
        const auto shadowedPlanePixels = [&](int frames) {
            renderN(frames);
            mirror.applyCamera(cam, view);
            renderN(frames);
            std::vector<float> lum;
            for (unsigned y = 0; y < kSize; ++y)
                for (unsigned x = 0; x < kSize; ++x) {
                    const Colour c = img.at(x, y);
                    if (c.r > 1.6f * c.g + 0.02f) continue;          // the red caster itself
                    lum.push_back((c.r + c.g + c.b) / 3.0f);
                }
            if (lum.empty()) return std::pair<int, float>(0, 0.0f);
            std::vector<float> sorted = lum;
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size() * 9 / 10, sorted.end());
            const float lit = sorted[sorted.size() * 9 / 10];
            int dark = 0;
            for (float l : lum) if (l < 0.7f * lit) ++dark;
            return std::pair<int, float>(dark, lit);
        };
        const auto withShadow = shadowedPlanePixels(8);
        caster->setShadowCastingEnabled(false);
        const auto without = shadowedPlanePixels(8);
        std::printf("   E1: shadowed plane pixels %d (lit %.3f) with the caster casting, %d (lit %.3f) "
                    "with its casting off\n", withShadow.first, withShadow.second, without.first,
                    without.second);
        CHECK(withShadow.first > 200 && without.first < withShadow.first / 10,
              "E1: THE PLANE RECEIVES SHADOWS — a caster above it darkens it, and the same caster "
              "with casting off does not");
        caster->removeFromParent();

        // E2: under the plane.
        auto under = iris::MeshNode::create();
        under->setName("Under");
        under->setMesh(testmesh::load(":/assets/models/cube.obj"));
        under->setMaterial(grey);
        under->setLocalPos(iris::Vec3(0.0f, -4.0f, -6.0f));
        doc->getRootNode()->addChild(under);
        cam->setLocalPos(iris::Vec3(0.0f, -0.5f, 0.0f));
        cam->lookAt(iris::Vec3(0.0f, -3.0f, -6.0f));
        const auto boxTop = [&]() {
            renderN(8);
            mirror.applyCamera(cam, view);
            renderN(8);
            double sum = 0; int n = 0;
            for (unsigned y = kSize / 2 - 6; y < kSize / 2 + 6; ++y)
                for (unsigned x = kSize / 2 - 6; x < kSize / 2 + 6; ++x) {
                    const Colour c = img.at(x, y);
                    sum += (c.r + c.g + c.b) / 3.0; ++n;
                }
            return n ? sum / n : 0.0;
        };
        const double underOn = boxTop();
        mirror.setGroundPlane(false);
        const double underOff = boxTop();
        std::printf("   E2: the box under the plane reads %.4f with the plane on, %.4f with it off\n",
                    underOn, underOff);
        CHECK(!isSky(img.at(kSize / 2, kSize / 2)) && underOn < 0.98 && std::fabs(underOn - underOff) < 0.01,
              "E2: THE PLANE CASTS NOTHING — a lit box under it reads the same with the plane as without");
        under->removeFromParent();
        topSun->removeFromParent();
        sun->setVisible(true);
        doc->shadowEnabled = false;
        view->setShadows(false);
        cam->setLocalPos(iris::Vec3(0.0f, 1.6f, 0.0f));
        cam->lookAt(iris::Vec3(0.0f, 1.55f, -100.0f));
        renderN(4);
        mirror.applyCamera(cam, view);
    }
    mirror.setGroundPlane(false);

    // ---- D. THE PLAYER'S HIDE LANDS ON THE NEXT SYNC, IN A SCENE THE
    //         VERIFIER CANNOT COVER IN ONE PASS (PLAYER-FLOOR-1's fix round,
    //         the Fable read item 1) ---------------------------------------
    //
    // `SceneMirror::setHideDefaultFloor` is the Player's switch for the
    // project's "hide the default floor" setting, and it is carried out as ONE
    // AND TERM in the node walk's effective-visibility rule. A term is only
    // read where a node is VISITED — and a still frame runs no walk: the mirror
    // consumes the document's change list and rotates a 32-entry VERIFIER
    // slice. So the flip reached the renderer only when that slice happened to
    // contain the floor: exact in the default scene (four entries, one rotation
    // covers everything) and a 1 − 32/N lottery in any real one — a
    // `player.screenshot` from a stopped Player missing the hide in the Mirror
    // Room, or the on-screen Player showing the floor for up to N/32 frames
    // after an editor shot during play.
    //
    // THE FIXTURE, AND WHY IT LOOKS THE WAY IT DOES (each part measured while
    // writing this case):
    //
    //   * FORTY EXTRA ENTRIES, invisible, so one verifier rotation cannot cover
    //     the map — the scene A/B/C use is smaller than a single slice, which
    //     is why the first guard of this lane passed on the defect.
    //   * THE VERIFIER OFF for the flip: whether the rotation reaches the floor
    //     is exactly the accident this case must not depend on. With it off the
    //     only route left is the CHANGE LIST, which is the contract.
    //   * A NODE CARRYING `defaultFloor` (the term is per node, never a name),
    //     and the Ground plane widget OFF so nothing else covers its pixels.
    for (int i = 0; i < 40; ++i) {
        auto prop = iris::MeshNode::create();
        prop->setName(QStringLiteral("Prop%1").arg(i));
        prop->setMesh(testmesh::load(":/assets/models/cube.obj"));
        prop->setMaterial(grey);
        prop->setVisible(false);          // in the entry map, out of every frame
        doc->getRootNode()->addChild(prop);
    }
    auto platform = iris::MeshNode::create();
    platform->setName("Platform");
    platform->setMesh(testmesh::load(":/assets/models/cube.obj"));
    platform->setMaterial(grey);
    platform->defaultFloor = true;        // the flag the setting is about
    platform->setLocalPos(iris::Vec3(0.0f, 0.0f, -6.0f));
    platform->setLocalScale(iris::Vec3(2.0f, 2.0f, 2.0f));
    doc->getRootNode()->addChild(platform);
    cam->setLocalPos(iris::Vec3(0.0f, 1.0f, 0.0f));
    cam->lookAt(iris::Vec3(0.0f, 0.0f, -6.0f));
    renderN(8);
    mirror.applyCamera(cam, view);
    renderN(2);

    const auto centreIsSky = [&]() {
        int sky = 0, total = 0;
        for (unsigned y = kSize / 2 - 12; y < kSize / 2 + 12; ++y)
            for (unsigned x = kSize / 2 - 12; x < kSize / 2 + 12; ++x) {
                ++total;
                if (isSky(img.at(x, y))) ++sky;
            }
        return total ? double(sky) / double(total) : 0.0;
    };
    std::printf("   D: %llu entries; the platform covers the centre (%.1f%% sky there)\n",
                (unsigned long long)mirror.mirroredNodeCount(), 100.0 * centreIsSky());
    CHECK(centreIsSky() < 0.05, "D: the second default-floor node is in the picture");

    mirror.setVerifierBudget(0);          // see the note above

    // THE FLIP, with the document UNTOUCHED, and exactly ONE sync after it.
    mirror.setHideDefaultFloor(true);
    mirror.sync();
    engine->renderOneFrame();
    view->readPixels(img);
    const double skyAfterFlip = centreIsSky();
    std::printf("   D: one sync after setHideDefaultFloor(true): %.1f%% sky at the centre\n",
                100.0 * skyAfterFlip);
    CHECK(skyAfterFlip > 0.95,
          "D: THE PLAYER'S HIDE IS IN THE VERY NEXT FRAME — a default floor leaves the picture "
          "on the sync after the flip, not when the verifier's 32-entry slice next reaches it");

    // AND BACK, one sync again: the editor states `false` before its own sync,
    // so the same guarantee has to hold in the other direction.
    mirror.setHideDefaultFloor(false);
    mirror.sync();
    engine->renderOneFrame();
    view->readPixels(img);
    const double skyAfterRestore = centreIsSky();
    std::printf("   D: one sync after setHideDefaultFloor(false): %.1f%% sky at the centre\n",
                100.0 * skyAfterRestore);
    CHECK(skyAfterRestore < 0.05,
          "D: ...and the editor gets it back in ITS very next frame");
    mirror.setVerifierBudget(32);         // as it was found

    std::printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
