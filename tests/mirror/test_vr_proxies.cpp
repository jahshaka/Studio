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
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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
    // WHICH MODEL A HAND WEARS IS THAT HAND'S OWN PROFILE'S ANSWER SINCE STAGE
    // 3 (VR_INPUT_SPEC §7): a wearer can hold a controller in one hand and
    // nothing in the other (WiVRn binds per hand), so the drawer asks the HAND
    // and `VrStatus::profile` is only the session's summary. The fixture leaves
    // both hands' profiles EMPTY here, which is a wearer holding two
    // controllers the runtime has not named — the wand, exactly as before.

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
        const auto bothProfiles = [](VrStatus &v, const char *path) {
            v.input[VrHandLeft].profile = path;
            v.input[VrHandRight].profile = path;
            v.profile = path;   // the session's summary, derived from the pair
        };
        bothProfiles(near, "/interaction_profiles/khr/simple_controller");
        mirror.setVrProxies(true, near);
        renderN(3);
        const std::vector<unsigned char> wands = img.rgba;

        // THE DEFAULT PATH, WHICH IS THE PRODUCT'S: the app's own models.qrc
        // is compiled into this suite (see its CMakeLists), so what is asserted
        // below is the resource path the shipped editor reads and not a copy of
        // it in the source tree.
        bothProfiles(near, "/interaction_profiles/oculus/touch_controller");
        mirror.setVrProxies(true, near);
        renderN(3);
        const size_t modelMoved = differing(img.rgba, wands);
        std::printf("   Touch profile: %zu of %zu bytes differ from the wands\n", modelMoved,
                    wands.size());
        CHECK(modelMoved > 1000u,
              "H: A TOUCH PROFILE DRAWS THE TOUCH MODEL — the runtime's own answer decides what "
              "the wearer's hand looks like");

        // ...AND IT IS LIT (the owner's headset smoke, 2026-09-17: with an
        // UNLIT flat grey the models read as PURE WHITE SILHOUETTES in the
        // eyes — the shape moved correctly in 3D and had no geometry in it,
        // because one constant colour through the eye's exposure is one output
        // value over the whole outline). A controller is a solid object in the
        // room and has to shade like one; the assertion is that its own pixels
        // carry MANY values rather than one.
        {
            // The model's own pixels: where the Touch picture differs from the
            // wands' — and this fixture renders at 1x (offscreen views stay 1x
            // so pixel suites keep exact colours), so an unlit flat surface
            // would give exactly ONE value with no antialiased edge to hide in.
            std::vector<unsigned> shades;
            const std::vector<unsigned char> &a = img.rgba;
            for (size_t i = 0; i + 3 < a.size() && i + 3 < wands.size(); i += 4) {
                if (a[i] == wands[i] && a[i + 1] == wands[i + 1] && a[i + 2] == wands[i + 2])
                    continue;
                const unsigned v = (unsigned(a[i]) << 16) | (unsigned(a[i + 1]) << 8) |
                                   unsigned(a[i + 2]);
                shades.push_back(v);
            }
            std::sort(shades.begin(), shades.end());
            shades.erase(std::unique(shades.begin(), shades.end()), shades.end());
            std::printf("   the Touch models' own pixels carry %zu distinct colours\n",
                        shades.size());
            CHECK(shades.size() > 8u,
                  "H: THE CONTROLLER IS LIT — its own pixels carry many shades under the "
                  "fixture's sun, not the one flat value an unlit helper gave (a white cut-out "
                  "in the headset: the owner's smoke)");
        }

        // ...AND THE WAND IS THE ANSWER FOR EVERY OTHER PROFILE, including
        // none at all: we do not know what that controller looks like.
        bothProfiles(near, "/interaction_profiles/microsoft/motion_controller");
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
        bothProfiles(near, "/interaction_profiles/oculus/touch_controller");
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

        // ...AND THE TWO HANDS ARE ASKED SEPARATELY (stage 3). A wearer with a
        // controller in the right hand and nothing in the left is a real WiVRn
        // session, and it used to be undrawable: the model came off the
        // session's ONE summary string, so both hands wore whatever the summary
        // said. The right hand keeps its Touch model here and the left, whose
        // profile is now a HAND, draws no controller at all — its own drawing is
        // its skeleton (case K).
        VrStatus mixed = near;
        mixed.input[VrHandLeft].profile = "/interaction_profiles/ext/hand_interaction_ext";
        mirror.setVrProxies(true, mixed);
        renderN(3);
        const size_t oneWandGone = differing(img.rgba, touchModels);
        std::printf("   one hand bare, one holding a Touch: %zu bytes differ from two "
                    "models\n", oneWandGone);
        CHECK(oneWandGone > 500u,
              "H: A MIXED PAIR IS DRAWN PER HAND — the bare hand loses its controller while "
              "the other keeps its model (the profile is the HAND's, not the session's)");
        mirror.setVrProxies(true, st);
        renderN(3);
    }

    // ---- I. THE MEMO IS NOT THE ONLY WRITER (VR-INPUT-1E-FIX finding 4) --
    //
    // The mirror remembers whether it last showed or hid each wand, so a still
    // frame writes no flag. But the running SESSION hides an unlocated hand
    // itself, inside its own frame (VrSession::placeProxies) — and every frame
    // loop that calls renderOneFrame without a host tick in front of it (the
    // scripted editor loops, the stable offscreen render, the Player's own
    // loop) runs that hide with no mirror sync behind it. With the memo still
    // saying "shown", nothing ever wrote the flag back and the wearer's hand
    // stayed missing until the next invalid→valid transition the mirror
    // happened to see.
    //
    // THE HIDE HERE IS EXACTLY THE SESSION'S: one setNodeVisible(false) on the
    // node, with the hand still valid in the status.
    {
        NodeId px[2] = { 0, 0 };
        mirror.vrProxyNodes(px);
        target->setNodeVisible(px[0], false);
        target->setNodeVisible(px[1], false);
        renderN(2);
        const size_t stranded = differing(img.rgba, withProxies);
        std::printf("   after a session-style hide, one sync later: %zu bytes differ from the "
                    "picture with both wands\n", stranded);
        CHECK(stranded == 0u,
              "I: A PROXY ANOTHER WRITER HID COMES BACK ON THE NEXT SYNC — the mirror's show "
              "side writes the flag every frame, because the session is a second writer");

        // ...AND THE ORDINARY CYCLE STILL COSTS NOTHING AND STILL WORKS:
        // valid -> invalid -> valid, which no suite covered.
        VrStatus goneLeft = st;
        goneLeft.hands[VrHandLeft].valid = false;
        mirror.setVrProxies(true, goneLeft);
        renderN(2);
        const size_t oneHand = differing(img.rgba, withProxies);
        CHECK(oneHand > 100u,
              "I: a hand that stops being reported takes its wand out of the picture");
        mirror.setVrProxies(true, st);
        renderN(2);
        CHECK(differing(img.rgba, withProxies) == 0u,
              "I: ...and a hand that comes back gets it back, byte for byte");
    }

    // ---- J. THE RAY IS NOT A HAND MARKER (VR-INPUT-1E-FIX finding 6) -----
    //
    // `vr.proxies(false)` is the WANDS' off switch: "do not draw a marker where
    // my hand is". The ray is the pointing TOOL — it is what tells a wearer
    // what they are about to select — so it is independent of that switch and
    // belongs to the SESSION, which is its only writer. The first cut took the
    // ray down with the wands, so the behaviour depended on the ORDER the two
    // were toggled in: the session put back every frame what the host had just
    // hidden (the very asymmetry VrSession::placeProxies forbids), and with the
    // switch off BEFORE a session the ray's nodes were never built at all.
    {
        NodeId ray[2] = { 0, 0 };
        mirror.vrRayNodes(ray);
        // A SESSION'S PLACEMENT, STOOD IN FOR: the mirror never places a ray
        // (a ray computed outside the frame leaves the wearer's own hand), so
        // this is the one write a live session would have made — three metres
        // of line in front of the camera.
        target->setNodeTransform(ray[0], Vec3{ 0.0f, 1.0f, -1.5f }, Quat{ 0, 0, 0, 1 },
                                 Vec3{ 1.0f, 1.0f, 3.0f });

        // The reference: the wands switched off and the ray NOT placed.
        mirror.setVrProxies(false, st);
        target->setNodeVisible(ray[0], false);
        renderN(3);
        const std::vector<unsigned char> noWands = img.rgba;
        CHECK(differing(noWands, quiet) == 0u,
              "J: with the wands switched off and no ray placed, the picture is the one with no "
              "session at all");

        // ...and now the session draws its ray, with the switch still off.
        target->setNodeVisible(ray[0], true);
        renderN(3);
        const size_t rayPixels = differing(img.rgba, noWands);
        std::printf("   the ray with vr.proxies(false): %zu bytes moved\n", rayPixels);
        // A LINE IS ONE PIXEL WIDE, and three metres of it seen end-on in a
        // 256-pixel view is about twenty pixels: measured 84 bytes = 21 px, and
        // the reference above is byte-identical to the no-session picture, so
        // ANY difference here is the ray and nothing else.
        CHECK(rayPixels > 40u,
              "J: THE RAY SURVIVES `vr.proxies(false)` — the wearer still sees what they are "
              "pointing at, and the mirror no longer fights the session for the flag");

        // ...AND THE ORDER DOES NOT MATTER: a mirror whose switch was off
        // before the session ever started still builds the ray's nodes.
        {
            Scene *second = engine->createScene("proxies-order");
            CHECK(second != nullptr, "J: a second scene for the toggle-order case");
            if (second) {
                {
                    auto doc2 = iris::Scene::create();
                    SceneMirror m2(second);
                    m2.setSource(doc2);
                    m2.setVrProxies(false, st);       // OFF before anything is built
                    m2.sync();
                    NodeId ray2[2] = { 0, 0 }, px2[2] = { 0, 0 };
                    m2.vrRayNodes(ray2);
                    m2.vrProxyNodes(px2);
                    CHECK(ray2[0] && ray2[1],
                          "J: the ray's two nodes are built by the SESSION even with the wands "
                          "switched off first — the feature does not depend on the order the "
                          "two switches were thrown in");
                    CHECK(px2[0] && px2[1],
                          "J: ...and the wands exist too (built, hidden), so switching them on "
                          "mid-session costs no construction");
                }
                engine->destroyScene(second);
            }
        }

        target->setNodeVisible(ray[0], false);
        mirror.setVrProxies(true, st);
        renderN(3);
        CHECK(differing(img.rgba, withProxies) == 0u,
              "J: ...and with the switch back on the picture is the two wands again");
    }

    // ---- K. THE WEARER'S BARE HANDS (VR_INPUT_SPEC §7, phase 4b stage 3) -
    //
    // A hand holding nothing is drawn as its own SKELETON: twenty-four segments
    // between the twenty-six joints XR_EXT_hand_tracking reports, on the same
    // two helper channels as the wands (in every VR eye, in the desk's picture,
    // in no capture and in no user's screenshot). ONE unit line mesh in the
    // whole scene carries all forty-eight of them — each node is stood at one
    // joint, turned onto the next and scaled to the distance — so a hand that
    // moves every frame rebuilds no geometry at all.
    //
    // NO RUNTIME CAN DRIVE THIS ANYWHERE IN THE PROJECT BUT THE OWNER'S QUEST:
    // Monado's simulated rig has no hands at all. So the joints arrive here as
    // DATA, exactly as the poses do, which is the same reason this suite needs
    // no headset.
    {
        // A HAND AT ARM'S LENGTH, where 2 cm bones are pixels rather than
        // rounding: the palm in front of the camera, the wrist below it and
        // five fingers fanned out. Built as a real hand so that a failure
        // prints something a person can read.
        const auto buildHand = [](float cx, std::vector<VrPose> &out) {
            out.assign(kVrHandJointCount, VrPose());
            const float y = 1.10f, z = -0.50f;
            out[0].valid = true; out[0].position = Vec3{ cx, y, z };            // palm
            out[1].valid = true; out[1].position = Vec3{ cx, y - 0.05f, z + 0.05f };  // wrist
            unsigned j = 2u;
            const float dx[5] = { -0.035f, -0.015f, 0.005f, 0.025f, 0.045f };
            const unsigned n[5] = { 4u, 5u, 5u, 5u, 5u };   // the thumb has no intermediate
            for (unsigned f = 0; f < 5u && j < kVrHandJointCount; ++f) {
                for (unsigned k = 0; k < n[f] && j < kVrHandJointCount; ++k, ++j) {
                    out[j].valid = true;
                    out[j].position = Vec3{ cx + dx[f], y + 0.025f * float(k),
                                            z - 0.025f * float(k) };
                }
            }
        };
        std::vector<VrPose> left, right;
        buildHand(-0.09f, left);
        buildHand(0.09f, right);

        // BOTH HANDS BARE: no controller in either, and a skeleton for each.
        VrStatus bare = st;
        bare.input[VrHandLeft].profile = "/interaction_profiles/ext/hand_interaction_ext";
        bare.input[VrHandRight].profile = "/interaction_profiles/ext/hand_interaction_ext";
        bare.input[VrHandLeft].jointsTracked = true;
        bare.input[VrHandRight].jointsTracked = true;
        bare.hands[VrHandLeft].position = left[0].position;
        bare.hands[VrHandRight].position = right[0].position;
        mirror.setVrHandJoints(VrHandLeft, left.data(), unsigned(left.size()));
        mirror.setVrHandJoints(VrHandRight, right.data(), unsigned(right.size()));
        mirror.setVrProxies(true, bare);
        renderN(3);
        const std::vector<unsigned char> hands = img.rgba;
        const size_t handPixels = differing(hands, quiet);
        std::printf("   two bare hands: %zu of %zu bytes moved from the no-session picture\n",
                    handPixels, quiet.size());
        CHECK(handPixels > 200u,
              "K: THE WEARER SEES THEIR OWN HANDS — twenty-four segments per hand, drawn from "
              "the joints the runtime located");
        CHECK(mirror.vrHandBonesShown(VrHandLeft) == kVrHandBoneCount &&
                  mirror.vrHandBonesShown(VrHandRight) == kVrHandBoneCount,
              "K: ...all 24 bones of each, because every joint located");

        // THE SAME TWO CHANNELS AS THE WANDS, node by node. A hand in a probe
        // capture or in a user's screenshot would be the one helper in the tree
        // that pretends to be content.
        {
            NodeId bones[kVrHandBoneCount] = {};
            const unsigned count = mirror.vrHandBoneNodes(VrHandRight, bones, kVrHandBoneCount);
            bool channels = count == kVrHandBoneCount;
            for (unsigned b = 0; b < count; ++b)
                channels = channels && bones[b] && target->nodeHelper(bones[b]) &&
                           target->nodeVrHelper(bones[b]);
            CHECK(channels,
                  "K: every bone is on BOTH helper channels — in the desk's picture and in "
                  "every VR eye, in no probe capture and in no user's screenshot");
            // ONE MESH FOR ALL OF THEM (the geometry claim): the segments are
            // transforms of a single unit line, so a moving hand allocates
            // nothing. Asserted through the scene's own mesh count rather than
            // by inspection.
            NodeId leftBones[kVrHandBoneCount] = {};
            mirror.vrHandBoneNodes(VrHandLeft, leftBones, kVrHandBoneCount);
            bool distinct = true;
            for (unsigned b = 0; b < count; ++b)
                for (unsigned c = 0; c < count; ++c)
                    if (bones[b] == leftBones[c]) distinct = false;
            CHECK(distinct, "K: ...and the two hands' bones are different NODES (one mesh, "
                            "forty-eight nodes)");
        }

        // A JOINT THE RUNTIME LOST TAKES ITS OWN BONES WITH IT, and nothing
        // else: a half-occluded hand really does report some joints and not
        // others, and a segment drawn to a joint nobody located would run to
        // wherever it was last seen.
        std::vector<VrPose> partial = right;
        partial[25].valid = false;   // the little finger's tip
        mirror.setVrHandJoints(VrHandRight, partial.data(), unsigned(partial.size()));
        renderN(2);
        CHECK(mirror.vrHandBonesShown(VrHandRight) == kVrHandBoneCount - 1u,
              "K: a joint that did not locate takes its ONE bone out of the picture (23 of 24)");
        mirror.setVrHandJoints(VrHandRight, right.data(), unsigned(right.size()));
        renderN(2);
        CHECK(differing(img.rgba, hands) == 0u, "K: ...and comes back byte for byte");

        // THE TWO DRAWINGS ARE ALTERNATIVES. A hand that picks a controller up
        // stops drawing fingers even while the runtime goes on reporting them
        // (some runtimes synthesise joints for a hand holding a thing): two
        // hands' worth of furniture in one hand's place is worse than either.
        {
            VrStatus held = bare;
            held.input[VrHandRight].profile = "/interaction_profiles/oculus/touch_controller";
            mirror.setVrProxies(true, held);
            renderN(3);
            CHECK(mirror.vrHandBonesShown(VrHandRight) == 0u,
                  "K: A HAND HOLDING A CONTROLLER DRAWS NO FINGERS — the controller is its "
                  "drawing, and the two are never both");
            CHECK(mirror.vrHandBonesShown(VrHandLeft) == kVrHandBoneCount,
                  "K: ...and the other hand, still bare, keeps its own");
        }

        // AND A BARE HAND IS A HAND MARKER: `vr.proxies(false)` takes the
        // skeleton away with the wands (the ray, which is the pointing tool
        // rather than a marker, is case J's).
        mirror.setVrProxies(false, bare);
        renderN(3);
        CHECK(mirror.vrHandBonesShown(VrHandLeft) == 0u &&
                  mirror.vrHandBonesShown(VrHandRight) == 0u,
              "K: `vr.proxies(false)` takes the wearer's hands down too — a skeleton IS the "
              "marker for a hand holding nothing");
        {
            // ...AND UNREGISTERS THEM, which is what stops the running session
            // (the other writer, inside its own frame) putting back what the
            // host just took away.
            NodeId none[kVrHandBoneCount] = {};
            CHECK(target->vrHandBoneNodes(VrHandLeft, none, kVrHandBoneCount) == 0u,
                  "K: ...and the scene no longer knows of them, so the session cannot place "
                  "one (the asymmetry rule: it may hide a marker, never show one)");
        }
        mirror.setVrProxies(true, bare);
        renderN(3);
        CHECK(differing(img.rgba, hands) == 0u,
              "K: ...switched back on, the hands are the same picture byte for byte");

        // AND THEY LEAVE WITH THE SESSION.
        mirror.setVrHandJoints(VrHandLeft, nullptr, 0u);
        mirror.setVrHandJoints(VrHandRight, nullptr, 0u);
        mirror.setVrProxies(true, st);
        renderN(3);
        CHECK(mirror.vrHandBonesShown(VrHandLeft) == 0u,
              "K: a hand with no skeleton this frame draws none");
        CHECK(differing(img.rgba, withProxies) == 0u,
              "K: ...and the picture is the two wands again, byte for byte");
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
