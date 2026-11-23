// engine.setters_refuse / engine.setters_refuse.posted - THE ENGINE's SETTERS REFUSE BY NAME
// (SCRIPT-IN-ENGINE-1 2e e1; OPEN_REDS:44 "the engine's scene setters swallow an unknown id"). C++ on purpose: the
// script host shields scripts (it asks a node's existence first), so only C++ reaches the engine's own path - the
// mirror's. The headless (NULL) engine: no card, no display.
//   setters (lockstep): the ten node setters given a REMOVED node each leave the engine's error naming the verb and the
//     node; the three VR node lists refuse a node the scene does not hold (0 still unregisters); a light whose IES
//     profile is not there is REFUSED naming the file, a repeated request too; a particle system whose colour ramp is
//     not there is REFUSED naming the file. THE CONTROL: the same calls on a live node leave no error; the refusals
//     counted are above zero.
//   posted (EngineConfig::hostRecords, the app's shape): each of the ten POSTED to a removed node is published as a
//     REFUSAL naming it at the frame that drains it. THE CONTROL: the live node's posts publish none.
#include "jahshaka/engine/Engine.h"#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Lights.h"
#include "jahshaka/engine/Particles.h"
#include "jahshaka/engine/Published.h"
#include "../support/enginetesthelpers.h"

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

/// The ten silent setters (OPEN_REDS:44), each a call on (scene, node).
struct Setter { const char *verb; std::function<void(Scene &, NodeId)> call; };
std::vector<Setter> theTen()
{
    return {
        {"Scene::setNodeTransform", [](Scene &s, NodeId n) { s.setNodeTransform(n, Vec3(1, 2, 3), Quat(), Vec3(1, 1, 1)); }},
        {"Scene::setNodeVisible", [](Scene &s, NodeId n) { s.setNodeVisible(n, false); }},
        {"Scene::setNodeVisibleUnder", [](Scene &s, NodeId n) { s.setNodeVisibleUnder(n, false, true); }},
        {"Scene::setNodeProbeGridExcluded", [](Scene &s, NodeId n) { s.setNodeProbeGridExcluded(n, true); }},
        {"Scene::setNodeHelper", [](Scene &s, NodeId n) { s.setNodeHelper(n, true); }},
        {"Scene::setNodeVrHelper", [](Scene &s, NodeId n) { s.setNodeVrHelper(n, true); }},
        {"Scene::setNodeBackdrop", [](Scene &s, NodeId n) { s.setNodeBackdrop(n, true); }},
        {"Scene::setNodeMovable", [](Scene &s, NodeId n) { s.setNodeMovable(n, true); }},
        {"Scene::setNodeLightMask", [](Scene &s, NodeId n) { s.setNodeLightMask(n, 0x0Fu); }},
        {"Scene::setNodeCastShadow", [](Scene &s, NodeId n) { s.setNodeCastShadow(n, false); }},
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
        check(missed.empty() && refused == 10,
              "THE TEN SETTERS on a removed node each leave the engine's error naming the verb and the node (" +
                  std::to_string(refused) + " of 10 refused); THE CONTROL: on a live node none leaves an error" + missed);
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
    // 4. A PARTICLE SYSTEM's MISSING COLOUR RAMP: setParticleSystem REFUSED naming the file
    {
        ParticleSystemDesc d;
        d.emitters.push_back(ParticleEmitterDesc{});
        ParticleAffectorDesc ramp;
        ramp.kind = ParticleAffectorDesc::Kind::ColourRamp;
        ramp.colourRampPath = "/no/such/dir/ramp.png";
        d.affectors.push_back(ramp);
        const bool set = s->setParticleSystem(live, d);
        const std::string err = e.takeLastError();
        check(!set && err.find("ramp.png") != std::string::npos,
              "A MISSING PARTICLE COLOUR RAMP: setParticleSystem is REFUSED naming it ('" + err + "')");
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
    for (const Setter &t : theTen()) t.call(*s, live);
    enginetest::frames(e, 1);
    const std::size_t liveRefusals = e.published().refusals.size();
    for (const Setter &t : theTen()) t.call(*s, dead);
    enginetest::frames(e, 1);
    std::string missed;
    int published = 0;
    for (const Setter &t : theTen()) {
        bool found = false;
        for (const auto &r : e.published().refusals) found = found || names(r.text, t.verb, dead);
        if (found) ++published;
        else missed += std::string("\n      ") + t.verb + ": no refusal published";
    }
    check(missed.empty() && published == 10 && liveRefusals == 0,
          "POSTED (EngineConfig::hostRecords): the ten setters on a removed node each publish a REFUSAL naming the verb "
          "and the node (" + std::to_string(published) + " of 10); THE CONTROL: the live node's posts publish " +
              std::to_string(liveRefusals) + missed);
    e.destroyScene(s);
    enginetest::frames(e, 1);
    return 0;
}

}  // namespace}  // namespace

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
