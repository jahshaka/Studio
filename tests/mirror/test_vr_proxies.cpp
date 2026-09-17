// mirror.vr_proxies — THE WEARER, DRAWN AT THE DESK AND NOWHERE ELSE
// (SPECS/VR_SPEC.md §5 phase 4, lane VR-4).
//
// WHAT THE PROXIES ARE. Two small wands, one at each of the wearer's
// controllers, drawn IN THE HEADSET (both the editor's preview and the
// Player's, the way a VR engine draws a wearer's own controllers) and in the
// desktop editor viewport, so somebody at the desk can see where the wearer is
// reaching. Mirror-owned line meshes — no document node, nothing in the
// outliner, nothing saved. THERE IS NO HEAD MARKER (owner, 2026-09-17): in the
// eyes it would be a box on the wearer's face, and on the desktop "where is the
// wearer" is already the camera.
//
// WHY THIS SUITE NEEDS NO HEADSET, AND THAT IS THE DESIGN. SceneMirror::
// setVrProxies takes a `jahshaka::engine::VrStatus` — the same struct the engine
// reports — as DATA. So a hand-built status with three poses in it drives the
// whole feature with no OpenXR runtime, no Monado and no GPU beyond the one
// every mirror suite already uses, and the claims below are pixels rather than
// inspection. The runtime half (do real poses arrive, do they ride the rig) is
// `vr.session`'s; the two-bit rule's EYE side (the headset shows the selection
// outline and not these) is `vr.session` case 8, which has a session to look
// through.
//
// THE FOUR CLAIMS, which are the owner's picture matrix at this level:
//   A. THE DESK sees them — a view with the ordinary helper channel open (the
//      editor viewport) draws the wearer's controllers;
//   B. A PICTURE THAT IS NOBODY'S — neither channel open: a thumbnail, a
//      preview, the Player's desktop window, the offscreen view a user's
//      screenshot renders through — is byte-for-byte the picture with no
//      session at all;
//   C. THE PLAYER'S EYE — the VR channel open and the ordinary one SHUT, which
//      is exactly the shape VrSession gives a Player's view — draws the
//      controllers and nothing else: no grid, no icons, no selection outline;
//   D. the flags say the same thing: each proxy is in BOTH channels, and the
//      SELECTION OUTLINE is in the desk's channel only (it reaches an editor
//      preview's eyes because THAT session opens the desk's channel, and it is
//      absent from a Player's for the same reason the grid is).
//
// PHASE 4b STAGE 1 ADDED TWO MORE (lane VR-INPUT-1E, VR_INPUT_SPEC §3):
//   G. THE RAY AND ITS HIT MARKER are two more nodes on the same two channels,
//      and they are HIDDEN until a live session places them — a host with no
//      session never shows a ray, and a node born visible would draw a metre
//      of line at the world origin for one frame;
//   H. THE CONTROLLER MODEL follows the runtime's own interaction profile: a
//      wearer holding a Touch controller gets the vendored Touch model
//      (app/content/vr/, MIT — PROVENANCE.md beside it), and every other
//      profile gets the hand-made wand, because a wand is the honest drawing
//      for a controller whose shape we do not know.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/materials/defaultmaterial.h"
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
    cfg.logFile = "test_vr_proxies-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("proxies", kSize, kSize, Colour(0.05f, 0.05f, 0.08f));
    Scene *target = engine->createScene("proxies");
    if (!view || !target) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setScene(target);
    target->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.3f, 0.3f, 0.3f));

    auto doc = iris::Scene::create();
    auto cube = iris::MeshNode::create();
    cube->setName("Cube");
    cube->setMesh(":/assets/models/cube.obj");
    auto grey = iris::DefaultMaterial::create();
    grey->setDiffuseColor(QColor(160, 160, 160));
    cube->setMaterial(grey);
    cube->setLocalPos(iris::Vec3(0.0f, 0.0f, -6.0f));
    doc->getRootNode()->addChild(cube);

    auto sun = iris::LightNode::create();
    sun->intensity = 1.2f;
    sun->setLocalRot(iris::Quat::fromEulerAngles(-45.0f, 20.0f, 0.0f));
    doc->getRootNode()->addChild(sun);

    auto cam = iris::CameraNode::create();
    cam->angle = 55.0f; cam->nearClip = 0.05f; cam->farClip = 500.0f;
    cam->setAspectRatio(1.0f);
    cam->setLocalPos(iris::Vec3(0.0f, 1.2f, 0.0f));
    cam->lookAt(iris::Vec3(0.0f, 1.0f, -6.0f));
    doc->getRootNode()->addChild(cam);

    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.sync();
    mirror.applyCamera(cam, view);

    Image img;
    const auto renderN = [&](int n) {
        for (int i = 0; i < n; ++i) { mirror.sync(); engine->renderOneFrame(); }
        view->readPixels(img);
    };
    const auto differing = [](const std::vector<unsigned char> &a,
                              const std::vector<unsigned char> &b) {
        size_t n = 0;
        const size_t m = std::min(a.size(), b.size());
        for (size_t i = 0; i < m; ++i) if (a[i] != b[i]) ++n;
        return n;
    };

    // ---- the picture with NO session at all ------------------------------
    renderN(6);
    const std::vector<unsigned char> quiet = img.rgba;

    // A SESSION'S WORTH OF POSES, hand-built. The head sits a little in front of
    // the camera and the two hands beside it — all three well inside the frame,
    // which is what makes "the picture moved" mean "the markers were drawn".
    VrStatus st;
    st.active = true;
    st.state = VrState::Focused;
    st.posesValid = true;
    st.headPosition = Vec3{ 0.0f, 1.1f, -2.0f };
    st.headRotation = Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
    st.handActions = true;
    st.hands[VrHandLeft].valid = true;
    st.hands[VrHandLeft].position = Vec3{ -0.35f, 0.9f, -1.8f };
    st.hands[VrHandLeft].rotation = Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
    st.hands[VrHandRight].valid = true;
    st.hands[VrHandRight].position = Vec3{ 0.35f, 0.9f, -1.8f };
    st.hands[VrHandRight].rotation = Quat{ 0.0f, 0.0f, 0.0f, 1.0f };

    // ---- A. the desk sees them -------------------------------------------
    mirror.setVrProxies(true, st);
    renderN(3);
    const std::vector<unsigned char> withProxies = img.rgba;
    const size_t drawn = differing(withProxies, quiet);
    std::printf("   with proxies: %zu of %zu bytes moved\n", drawn, quiet.size());
    CHECK(drawn > 500u, "A: THE DESK SEES THE WEARER'S CONTROLLERS");
    CHECK(mirror.vrProxies(), "A: ...and the mirror says so");

    // ---- D. the flags ----------------------------------------------------
    NodeId nodes[2] = { 0, 0 };
    mirror.vrProxyNodes(nodes);
    CHECK(nodes[0] && nodes[1], "D: two proxy nodes exist (left hand, right hand) — and no third");
    bool bothChannels = true;
    for (int i = 0; i < 2; ++i) {
        if (!nodes[i]) continue;
        bothChannels = bothChannels && target->nodeHelper(nodes[i]) &&
                       target->nodeVrHelper(nodes[i]);
    }
    CHECK(bothChannels,
          "D: each controller is in BOTH channels — kHelperBit (the desk, and out of every "
          "capture and user shot) and kVrHelperBit (every VR eye, both modes)");

    // ---- B. a picture that is nobody's -----------------------------------
    // NEITHER CHANNEL: a thumbnail, a preview, the Player's desktop window and
    // the offscreen view a user's screenshot renders through are all this shape.
    view->setHelpersVisible(false);
    renderN(3);
    const size_t leaked = differing(img.rgba, quiet);
    std::printf("   neither channel, proxies still pushed: %zu of %zu bytes differ from the "
                "no-session picture\n", leaked, quiet.size());
    CHECK(leaked == 0u,
          "B: A PICTURE THAT OPENS NEITHER CHANNEL HAS NO CONTROLLERS — byte-identical to the "
          "scene with no session at all");

    // ---- C. the Player's eye ---------------------------------------------
    // The VR channel open, the desk's SHUT: what VrSession gives a Player's
    // view. A selection is made first, so "no other furniture" has something to
    // be true about.
    mirror.setHighlightedNodes({ cube }, cube);
    renderN(3);
    const std::vector<unsigned char> playerEyeNoVr = img.rgba;
    CHECK(differing(playerEyeNoVr, quiet) == 0u,
          "C: with neither channel the selection outline is absent too (it is the desk's)");
    view->setVrHelpersVisible(true);
    renderN(3);
    const size_t playerSees = differing(img.rgba, playerEyeNoVr);
    std::printf("   the Player's eye shape (VR channel only): %zu bytes moved\n", playerSees);
    CHECK(playerSees > 500u,
          "C: THE PLAYER'S EYE DRAWS THE CONTROLLERS — the one thing a wearer must see when "
          "they are not editing");
    // ...and ONLY them: the picture is the no-session one plus the controllers,
    // which is what the desk's picture was NOT (it also carries the outline).
    view->setHelpersVisible(true);
    renderN(3);
    const size_t deskSees = differing(img.rgba, quiet);
    std::printf("   the editor's shape (both channels): %zu bytes moved\n", deskSees);
    CHECK(deskSees > playerSees,
          "C: ...and the EDITOR's shape draws more — the controllers AND the outline, grid and "
          "icons the wearer of an editor preview is meant to see");
    view->setVrHelpersVisible(false);

    // ---- E. THE SESSION IS TOLD WHICH NODES THEY ARE (VR-4-FIX finding 4) --
    // The mirror makes the markers; a running VrSession places them INSIDE its
    // own frame, right after it has located the hands, because that is the only
    // moment this frame's poses exist. It finds them through the scene, and
    // this is the registration that puts them there. (The placement itself
    // needs a runtime: `vr.verbs_session` measures the lag in frames.)
    {
        NodeId registered[2] = { 0, 0 };
        target->vrProxyNodes(registered);
        CHECK(registered[0] == nodes[0] && registered[1] == nodes[1] &&
              registered[0] && registered[1],
              "E: the scene knows both proxy nodes, left first — so a session can place them "
              "inside the frame it draws them in");
        // ...and the read-back the measurement uses answers for them.
        Vec3 pos;
        Quat rot;
        const bool got = target->nodeWorldPose(registered[0], pos, rot);
        std::printf("   left proxy world pose: %.3f %.3f %.3f (got=%d)\n",
                    double(pos.x), double(pos.y), double(pos.z), int(got));
        CHECK(got && std::abs(pos.x - st.hands[VrHandLeft].position.x) < 1e-4f &&
              std::abs(pos.y - st.hands[VrHandLeft].position.y) < 1e-4f &&
              std::abs(pos.z - st.hands[VrHandLeft].position.z) < 1e-4f,
              "E: nodeWorldPose reads the marker back where the hand was put — the read half of "
              "the lag measurement");
        CHECK(!target->nodeWorldPose(999999u, pos, rot),
              "E: ...and refuses an id that is not a node");
    }

    // ---- F. A HELPER BILLBOARD IS IN THE WEARER'S CHANNEL TOO (finding 9) --
    // The two-bit rule used to reach Items only: a billboard set marked
    // vrHelper carried kHelperBit alone, so a marker built as a billboard —
    // phase 4b's hit marker, most obviously — would have been desk-only,
    // silently. The Player's eye shape is where that shows.
    {
        const NodeId quad = target->createNode();
        CHECK(quad != 0, "F: a node for a helper billboard");
        target->setNodeHelper(quad, true);
        target->setNodeTransform(quad, Vec3{ 0.0f, 1.0f, -3.0f }, Quat{ 0, 0, 0, 1 },
                                 Vec3{ 1, 1, 1 });
        CHECK(target->createBillboardSet(quad, 0, false, 1), "F: ...with one billboard on it");
        BillboardInstance b;
        b.position = Vec3{ 0.0f, 1.0f, -3.0f };
        b.size = 0.5f;
        b.colour = Colour(1.0f, 0.2f, 0.8f, 1.0f);
        target->setBillboards(quad, &b, 1);

        // The Player's eye: the VR channel only. Not a vrHelper yet — invisible.
        view->setHelpersVisible(false);
        view->setVrHelpersVisible(true);
        renderN(3);
        const std::vector<unsigned char> beforeBit = img.rgba;
        target->setNodeVrHelper(quad, true);
        renderN(3);
        const size_t moved = differing(img.rgba, beforeBit);
        std::printf("   helper billboard given the VR channel: %zu bytes moved\n", moved);
        CHECK(moved > 200u,
              "F: A HELPER BILLBOARD JOINS THE WEARER'S CHANNEL — setNodeVrHelper reaches "
              "billboard sets, not only Items");
        target->removeNode(quad);
        view->setVrHelpersVisible(false);
        view->setHelpersVisible(true);
        renderN(3);
    }

    // ---- D (second half). the outline is the desk's only ------------------
    unsigned helperCount = 0, vrHelperCount = 0;
    for (NodeId id = 1; id < 4096; ++id) {
        if (!target->nodeHelper(id)) continue;
        ++helperCount;
        if (target->nodeVrHelper(id)) ++vrHelperCount;
    }
    std::printf("   scene helpers: %u, of which VR helpers: %u\n", helperCount, vrHelperCount);
    // FOUR SINCE PHASE 4b STAGE 1, RE-ANCHORED WITH ITS REASON (it was two):
    // the two controllers, plus the RAY and its HIT MARKER. They are the
    // wearer's own furniture by exactly the same argument — a pointer a wearer
    // cannot see is not a pointer — and nothing else in the tree may join this
    // channel without saying so here.
    CHECK(vrHelperCount == 4u,
          "D: EXACTLY FOUR THINGS ARE IN THE VR CHANNEL — the two controllers and the ray with "
          "its hit marker. The selection outline, the grid and the wires are the desk's, and a "
          "Player's eyes show none of them");
    mirror.setHighlightedNodes({}, iris::SceneNodePtr());

    // ---- G. THE RAY AND ITS HIT MARKER (phase 4b stage 1) ----------------
    {
        NodeId ray[2] = { 0, 0 };
        mirror.vrRayNodes(ray);
        CHECK(ray[0] && ray[1] && ray[0] != nodes[0] && ray[0] != nodes[1],
              "G: the ray has two nodes of its own — the line and the hit marker");
        bool channels = true;
        for (int i = 0; i < 2; ++i)
            channels = channels && target->nodeHelper(ray[i]) && target->nodeVrHelper(ray[i]);
        CHECK(channels,
              "G: ...both on the wearer's two channels: every VR eye and the desk's picture, no "
              "capture and no user's screenshot");
        NodeId registered[2] = { 0, 0 };
        target->vrRayNodes(registered);
        CHECK(registered[0] == ray[0] && registered[1] == ray[1],
              "G: the scene knows both, the LINE first — so a session can place them inside the "
              "frame that draws them (Engine::setVrRay)");
        // HIDDEN WITH NO SESSION. The mirror never places a ray: the session
        // is its only writer, because a ray computed outside the frame leaves
        // the wearer's own hand. So with a hand-built status and no runtime the
        // picture must be exactly the picture with the two wands in it.
        renderN(3);
        CHECK(differing(img.rgba, withProxies) == 0u,
              "G: with no live session the ray is not drawn at all — the picture is the two "
              "controllers and nothing else");
    }

    // ---- H. THE CONTROLLER MODEL FOLLOWS THE PROFILE ---------------------
    {
        // CLOSE ENOUGH TO READ. A 7 cm controller at 1.8 m is ten pixels wide
        // in a 256-pixel view; the model case therefore brings the hands to
        // arm's length, where the difference between a wand and a solid model
        // is a picture and not a rounding.
        VrStatus near = st;
        near.hands[VrHandLeft].position = Vec3{ -0.20f, 1.10f, -0.55f };
        near.hands[VrHandRight].position = Vec3{ 0.20f, 1.10f, -0.55f };
        near.profile = "/interaction_profiles/khr/simple_controller";
        mirror.setVrProxies(true, near);
        renderN(3);
        const std::vector<unsigned char> wands = img.rgba;

        // THE DEFAULT PATH, WHICH IS THE PRODUCT'S: the app's own models.qrc
        // is compiled into this suite (see its CMakeLists), so what is asserted
        // below is the resource path the shipped editor reads and not a copy of
        // it in the source tree.
        near.profile = "/interaction_profiles/oculus/touch_controller";
        mirror.setVrProxies(true, near);
        renderN(3);
        const size_t modelMoved = differing(img.rgba, wands);
        std::printf("   Touch profile: %zu of %zu bytes differ from the wands\n", modelMoved,
                    wands.size());
        CHECK(modelMoved > 1000u,
              "H: A TOUCH PROFILE DRAWS THE TOUCH MODEL — the runtime's own answer decides what "
              "the wearer's hand looks like");

        // ...AND THE WAND IS THE ANSWER FOR EVERY OTHER PROFILE, including
        // none at all: we do not know what that controller looks like.
        near.profile = "/interaction_profiles/microsoft/motion_controller";
        mirror.setVrProxies(true, near);
        renderN(3);
        CHECK(differing(img.rgba, wands) == 0u,
              "H: ...and WMR — or bare hands, or nothing bound — gets the wand back, byte for "
              "byte");

        // ...AND THE SLOT ITSELF (the owner's "a slot games can fill later").
        // A DIFFERENT SHAPE, deliberately: the vendored pair are mirror images
        // of each other and the fixture is nearly symmetric, so "the models
        // swapped between the hands" could read as the same picture even when
        // the swap worked. A cone cannot.
        near.profile = "/interaction_profiles/oculus/touch_controller";
        mirror.setVrProxies(true, near);
        renderN(3);
        const std::vector<unsigned char> touchModels = img.rgba;
        mirror.setVrProxyModels(QStringLiteral(":/content/primitives/cone.obj"),
                                QStringLiteral(":/content/primitives/cone.obj"));
        renderN(3);
        const size_t slotMoved = differing(img.rgba, touchModels);
        std::printf("   the model slot, pointed at a cone: %zu bytes differ from the Touch "
                    "models\n", slotMoved);
        CHECK(slotMoved > 1000u,
              "H: THE MODEL SLOT TAKES A HOST'S OWN PATHS — and a path that CHANGES is really "
              "reloaded (the first cut returned the cached mesh before it looked at the path, "
              "so a host's model was accepted and silently ignored)");
        mirror.setVrProxyModels(
            QStringLiteral(":/content/vr/meta-quest-touch-pro/left.obj"),
            QStringLiteral(":/content/vr/meta-quest-touch-pro/right.obj"));
        renderN(3);
        CHECK(differing(img.rgba, touchModels) == 0u,
              "H: ...and the vendored pair comes back byte for byte");
        mirror.setVrProxies(true, st);
        renderN(3);
    }

    // ---- ...and the proxies go when the session does ----------------------
    mirror.setHighlightedNodes({}, iris::SceneNodePtr());
    st.active = false;
    mirror.setVrProxies(false, st);
    renderN(3);
    CHECK(differing(img.rgba, quiet) == 0u,
          "the proxies leave with the session: the picture is exactly what it was before one");

    std::printf("\n%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
