// engine.setters_refuse / engine.setters_refuse.posted - THE ENGINE's SETTERS REFUSE BY NAME
// (SCRIPT-IN-ENGINE-1 2e e1; OPEN_REDS:44 "the engine's scene setters swallow an unknown id"). C++ on purpose: the
// script host shields scripts (it asks a node's existence first), so only C++ reaches the engine's own path - the
// mirror's. The headless (NULL) engine: no card, no display.
//   setters (lockstep): the eleven node setters (the ten of OPEN_REDS:44 and setNodeFaceCull) given a REMOVED node each leave the engine's error naming the verb and the
//     node; the three VR node lists refuse a node the scene does not hold (0 still unregisters); a light whose IES
//     profile is not there is REFUSED naming the file, a repeated request too; a particle system whose colour ramp is
//     not there is REFUSED naming the file. THE CONTROL: the same calls on a live node leave no error; the refusals
//     counted are above zero.
//   posted (EngineConfig::hostRecords): each of the eleven POSTED to a removed node is published as a REFUSAL naming it
//     at the frame that drains it - by a direct call (the scene's own post) AND THROUGH THE APP's ROUTE, scenecmd::call
//     (irisgl/mirror/scenecommands.h: the mirror's, the overlays', the VR arc's), whose wrapper accepts a void verb: the
//     drain counts an accepted message that LEFT AN ERROR as refused (OgreEngine::applySceneMessage). THE CONTROL: the
//     live node's posts publish none.
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Lights.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Particles.h"
#include "jahshaka/engine/Published.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenecommands.h"   // THE APP's ROUTE: every product call of a setter

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    std::fflush(stdout);
    if (!ok) ++failures;
}

namespace {

/// Frames after which a frame's published refusals have aged out of the window (PublishedEngineFrame::kFrameErrorsKept).
constexpr int kFrameErrorsGone = int(PublishedEngineFrame::kFrameErrorsKept) + 1;

/// The eleven silent setters (OPEN_REDS:44's ten and setNodeFaceCull, the audit's 5th), each a call on (scene, node):
/// direct, and through the app's route (scenecmd::call: a message, its void verb "accepted" by the wrapper).
struct Setter {
    const char *verb;
    std::function<void(Scene &, NodeId)> call;
    std::function<void(Scene *, NodeId)> wrapped;
};
std::vector<Setter> theTen()
{
    using scenecmd::call;
    return {
        {"Scene::setNodeTransform", [](Scene &s, NodeId n) { s.setNodeTransform(n, Vec3(1, 2, 3), Quat(), Vec3(1, 1, 1)); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeTransform, n, Vec3(1, 2, 3), Quat(), Vec3(1, 1, 1)); }},
        {"Scene::setNodeVisible", [](Scene &s, NodeId n) { s.setNodeVisible(n, false); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeVisible, n, false); }},
        {"Scene::setNodeVisibleUnder", [](Scene &s, NodeId n) { s.setNodeVisibleUnder(n, false, true); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeVisibleUnder, n, false, true); }},
        {"Scene::setNodeProbeGridExcluded", [](Scene &s, NodeId n) { s.setNodeProbeGridExcluded(n, true); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeProbeGridExcluded, n, true); }},
        {"Scene::setNodeHelper", [](Scene &s, NodeId n) { s.setNodeHelper(n, true); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeHelper, n, true); }},
        {"Scene::setNodeVrHelper", [](Scene &s, NodeId n) { s.setNodeVrHelper(n, true); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeVrHelper, n, true); }},
        {"Scene::setNodeBackdrop", [](Scene &s, NodeId n) { s.setNodeBackdrop(n, true); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeBackdrop, n, true); }},
        {"Scene::setNodeMovable", [](Scene &s, NodeId n) { s.setNodeMovable(n, true); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeMovable, n, true); }},
        {"Scene::setNodeLightMask", [](Scene &s, NodeId n) { s.setNodeLightMask(n, 0x0Fu); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeLightMask, n, 0x0Fu); }},
        {"Scene::setNodeCastShadow", [](Scene &s, NodeId n) { s.setNodeCastShadow(n, false); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeCastShadow, n, false); }},
        {"Scene::setNodeFaceCull", [](Scene &s, NodeId n) { s.setNodeFaceCull(n, FaceCull::TwoSided); },
         [](Scene *s, NodeId n) { call(s, &Scene::setNodeFaceCull, n, FaceCull::TwoSided); }},
    };
}

bool names(const std::string &err, const char *verb, NodeId n)
{
    return err.find(verb) != std::string::npos && err.find("node " + std::to_string(n) + " ") != std::string::npos;
}

int setters(Engine &e)
{
    Scene *s = e.createScene("setters");
    const NodeId live = s->createNode(), dead = s->createNode();
    s->removeNode(dead);
    e.takeLastError();
    // 1. THE TEN, on a removed node: each names itself and the node; on a live node: no error
    {
        std::string missed;
        int refused = 0;
        for (const Setter &t : theTen()) {
            t.call(*s, dead);
            const std::string err = e.takeLastError();
            if (names(err, t.verb, dead)) ++refused;
            else missed += std::string("\n      ") + t.verb + " on a removed node: '" + err + "'";
            t.call(*s, live);
            const std::string ok = e.takeLastError();
            if (!ok.empty()) missed += std::string("\n      ") + t.verb + " on a live node: '" + ok + "'";
        }
        check(missed.empty() && refused == 11,
              "THE ELEVEN SETTERS on a removed node each leave the engine's error naming the verb and the node (" +
                  std::to_string(refused) + " of 11 refused); THE CONTROL: on a live node none leaves an error" + missed);
    }
    // 2. THE THREE VR NODE LISTS: a node the scene does not hold is refused by name and NOT registered (the list in force
    //    stays); 0 still unregisters; a hand past the two refused
    {
        s->setVrProxyNodes(live, 0);
        const std::string okProxy = e.takeLastError();
        s->setVrProxyNodes(dead, live);
        const std::string badProxy = e.takeLastError();
        s->setVrRayNodes(0, dead);
        const std::string badRay = e.takeLastError();
        const NodeId one[1] = {live};
        s->setVrHandBoneNodes(0, one, 1);
        const std::string okBones = e.takeLastError();
        const NodeId bones[2] = {live, dead};
        s->setVrHandBoneNodes(0, bones, 2);
        const std::string badBones = e.takeLastError();
        s->setVrHandBoneNodes(2, bones, 1);
        const std::string badHand = e.takeLastError();
        NodeId held[2] = {0, 0};
        const unsigned count = s->vrHandBoneNodes(0, held, 2);   // (the list in force: the one live bone)
        check(okProxy.empty() && names(badProxy, "Scene::setVrProxyNodes", dead) &&
                  names(badRay, "Scene::setVrRayNodes", dead) && okBones.empty() &&
                  names(badBones, "Scene::setVrHandBoneNodes", dead) && count == 1 && held[0] == live &&
                  badHand.find("Scene::setVrHandBoneNodes: hand 2") != std::string::npos,
              "THE VR NODE LISTS refuse a node the scene does not hold by name, the list in force kept (" + badProxy +
                  "; " + badRay + "; " + badBones + "; " + badHand + "); THE CONTROL: a live node and 0 register silently");
    }
    // 2b. A REMOVED NODE LEAVES THE VR LISTS (the audit's lower list): a hand's bone list naming a node, the node removed
    //     - the list reads 0 in its place (the session skips a 0: no setter refused every VR frame). The proxies' and the
    //     ray's pairs are cleared the same way; they have no public read and no VR frame runs headless: the VR rig's case
    {
        const NodeId a = s->createNode(), b = s->createNode();
        const NodeId pair[2] = {a, b};
        s->setVrHandBoneNodes(1, pair, 2);
        s->removeNode(b);
        NodeId held[2] = {7, 7};
        const unsigned count = s->vrHandBoneNodes(1, held, 2);
        check(count == 2 && held[0] == a && held[1] == 0 && e.takeLastError().empty(),
              "A REMOVED NODE LEAVES THE VR LISTS: a hand's bone list read [" + std::to_string(held[0]) + ", " +
                  std::to_string(held[1]) + "] after its second bone's node went (0 in its place); THE CONTROL: the live "
                  "bone stays");
        s->setVrHandBoneNodes(1, nullptr, 0);
        s->removeNode(a);
    }
    // 3. A LIGHT's IES PROFILE THAT IS NOT THERE: setLight REFUSED naming the file, a repeated request too (not retried,
    //    still refused); a directional light reads no profile (no check); THE CONTROL: no profile is set silently
    {
        LightDesc spot;
        spot.type = LightType::Spot;
        const bool plain = s->setLight(live, spot);
        const std::string plainErr = e.takeLastError();
        spot.iesProfilePath = "/no/such/dir/missing.ies";
        const bool first = s->setLight(live, spot);
        const std::string firstErr = e.takeLastError();
        const bool again = s->setLight(live, spot);
        const std::string againErr = e.takeLastError();
        LightDesc sun = spot;
        sun.type = LightType::Directional;
        const bool directional = s->setLight(live, sun);
        e.takeLastError();
        check(plain && plainErr.empty() && !first && firstErr.find("missing.ies") != std::string::npos && !again &&
                  againErr == firstErr && directional,
              "A MISSING IES PROFILE: setLight is REFUSED naming it ('" + firstErr + "'), the same request again too; a "
              "directional light reads none; THE CONTROL: a light with no profile is set silently");
        s->removeLight(live);
    }
    // 4. A PARTICLE SYSTEM's MISSING COLOUR RAMP: setParticleSystem REFUSED naming the file; a ramp affector with an EMPTY
    //    path is "no ramp image yet" - a legal record (engine.particles.set sends every ramp so; the path comes by its
    //    own `files` command) - ACCEPTED, no error (the lead, 2026-11-23)
    {
        ParticleSystemDesc d;
        d.emitters.push_back(ParticleEmitterDesc{});
        ParticleAffectorDesc ramp;
        ramp.kind = ParticleAffectorDesc::Kind::ColourRamp;
        d.affectors.push_back(ramp);
        const bool empty = s->setParticleSystem(live, d);
        const std::string emptyErr = e.takeLastError();
        enginetest::frames(e, 1);
        const unsigned defs0 = s->published().particleDefinitionsCreated;
        // ...a NEW topology (a rotator added) with the ramp's image missing: REFUSED, and ATOMIC - nothing of the system
        // changed: no definition built for the new topology (published().particleDefinitionsCreated, Published.h:211)
        d.affectors[0].colourRampPath = "/no/such/dir/ramp.png";
        ParticleAffectorDesc spin;
        spin.kind = ParticleAffectorDesc::Kind::Rotator;
        d.affectors.push_back(spin);
        const bool set = s->setParticleSystem(live, d);
        const std::string err = e.takeLastError();
        enginetest::frames(e, 1);
        const unsigned defs1 = s->published().particleDefinitionsCreated;
        check(empty && emptyErr.empty() && defs0 > 0, "A RAMP AFFECTOR WITH AN EMPTY PATH (no ramp image yet) is ACCEPTED: "
                                                      "setParticleSystem true, no error ('" + emptyErr + "'), its definition "
                                                      "built (" + std::to_string(defs0) + ")");
        check(!set && err.find("ramp.png") != std::string::npos && defs1 == defs0,
              "A MISSING PARTICLE COLOUR RAMP: setParticleSystem is REFUSED naming it ('" + err + "'), ATOMIC: no "
              "definition built for the refused topology (" + std::to_string(defs0) + " -> " + std::to_string(defs1) + ")");
        s->removeParticleSystem(live);
        e.takeLastError();
    }
    e.destroyScene(s);
    return 0;
}

int posted(Engine &e)
{
    Scene *s = e.createScene("setters-posted");
    const NodeId live = s->createNode(), dead = s->createNode();
    enginetest::frames(e, 1);
    s->removeNode(dead);
    enginetest::frames(e, 1);
    // the refusals published naming each setter and `dead` since `seen` (the running count: input.messagesRefused)
    const auto count = [&](const char *route, std::string &missed) {
        int published = 0;
        for (const Setter &t : theTen()) {
            bool found = false;
            for (const auto &r : e.published().refusals) found = found || names(r.text, t.verb, dead);
            if (found) ++published;
            else missed += std::string("\n      ") + route + " " + t.verb + ": no refusal published";
        }
        return published;
    };
    // 1. A DIRECT CALL (the scene's own post under hostRecords)
    for (const Setter &t : theTen()) t.call(*s, live);
    enginetest::frames(e, 1);
    const unsigned long long liveDirect = e.published().input.messagesRefused;
    for (const Setter &t : theTen()) t.call(*s, dead);
    enginetest::frames(e, 1);
    std::string missed;
    const int direct = count("direct", missed);
    // 2. THE APP's ROUTE (scenecmd::call: the wrapper accepts a void verb; the drain publishes the error it left)
    enginetest::frames(e, kFrameErrorsGone);   // (the direct round's refusals age out of the published window)
    const unsigned long long beforeLive = e.published().input.messagesRefused;
    for (const Setter &t : theTen()) t.wrapped(s, live);
    enginetest::frames(e, 1);
    const unsigned long long liveWrapped = e.published().input.messagesRefused - beforeLive;
    for (const Setter &t : theTen()) t.wrapped(s, dead);
    enginetest::frames(e, 1);
    const int wrapped = count("wrapped", missed);
    check(missed.empty() && direct == 11 && wrapped == 11 && liveWrapped == 0,
          "POSTED (EngineConfig::hostRecords): the eleven setters on a removed node each publish a REFUSAL naming the "
          "verb and the node - by a direct call (" + std::to_string(direct) + " of 11) AND THROUGH THE APP's ROUTE, "
          "scenecmd::call (" + std::to_string(wrapped) + " of 11); THE CONTROL: the live node's posts publish none (" +
              std::to_string(liveWrapped) + " through the wrapper; the direct round's running count " +
              std::to_string(liveDirect) + ")" + missed);
    e.destroyScene(s);
    enginetest::frames(e, 1);
    return 0;
}

}  // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "setters" && mode != "posted") {
        std::printf("usage: test_setters_refuse setters|posted\n");
        return 2;
    }
    const char *row = mode == "setters" ? "engine.setters_refuse" : "engine.setters_refuse.posted";
    EngineConfig cfg = enginetest::testEngineConfig("test-setters-refuse-" + mode + "-ogre.log",
                                                    enginetest::EngineOverrides().headless());
    if (mode == "posted") cfg.hostRecords = true;
    std::string err;
    auto engine = Engine::create(cfg, err);
    check(engine != nullptr, "the headless (NULL) engine boots (" + err + ")");
    if (engine) {
        if (mode == "setters") setters(*engine);
        else posted(*engine);
    }
    if (failures) std::printf("%s: FAILED (%d)\n", row, failures);
    else std::printf("%s: PASSED\n", row);
    return failures ? 1 : 0;
}
