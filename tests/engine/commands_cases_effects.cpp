// script.commands - THE EFFECTS FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp runs them, in its
// fixed order): per command its declared domain refused by name on the null engine, per table its default and a
// filled value round-trip and inOrder, an engine-wide setter refused for an outside client, the effect where the
// headless engine answers it (each case prints what it read), and RULED 9's path law: engine.particles.set refuses a
// path, engine.particles.setColourRamp sets it on this host's own last record, engine.particles.remove erases that.
#include "script_commands.h"
#include "script/records_effects.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace scriptcommands {

namespace {

Value obj(Value::Object o) { return Value(std::move(o)); }
Value nums(std::initializer_list<double> xs)
{
    Value::Array a;
    for (double x : xs) a.push_back(Value(x));
    return Value(std::move(a));
}

void writeTga(const char *path)
{
    if (FILE *f = std::fopen(path, "wb")) {
        const unsigned char hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 32, 0x28};
        std::fwrite(hdr, 1, sizeof hdr, f);
        const std::vector<unsigned char> bgra(4 * 4 * 4, 200);
        std::fwrite(bgra.data(), 1, bgra.size(), f);
        std::fclose(f);
    }
}

/// The record through its table and back: equal as values (the records carry no operator== of their own).
template <class R> bool roundTrips(const R &in)
{
    R back;
    std::string why;
    return Record<R>::table().fromValue(toValue(in), back, "", why) && toValue(back) == toValue(in);
}

ParticleSystemDesc filledSystem()
{
    ParticleSystemDesc d;
    d.quota = 4000;
    d.additive = false;
    d.alphaHash = false;
    d.distortion = true;
    d.orientation = ParticleOrientation::PerpendicularSelf;
    d.commonDirection = Vec3(1, 0, 0);
    d.commonUp = Vec3(0, 0, 1);
    ParticleEmitterDesc e;
    e.shape = ParticleEmitterShape::HollowEllipsoid;
    e.position = Vec3(1, 2, 3);
    e.direction = Vec3(0, 0, 1);
    e.angleDegrees = 30;
    e.rate = 100;
    e.velocityMin = 0.5f; e.velocityMax = 2;
    e.ttlMin = 0.25f; e.ttlMax = 3;
    e.sizeWidth = 0.5f; e.sizeHeight = 0.25f;
    e.colourStart = Colour(2, 1, 0.5f, 1); e.colourEnd = Colour(0, 0, 0, 0);
    e.extents = Vec3(2, 3, 4); e.innerExtents = Vec3(1, 1, 1);
    e.duration = 1; e.repeatDelay = 0.5f; e.startTime = 0.25f;
    d.emitters = {e, ParticleEmitterDesc()};
    ParticleAffectorDesc a;
    a.kind = ParticleAffectorDesc::Kind::ColourKeys;
    a.keyCount = 3;
    a.colourKeys[1] = Colour(100, 4, -2, 1);
    a.colourKeyTimes[1] = 0.5f; a.colourKeyTimes[2] = 1;
    a.scaleKeys[2] = 4; a.scaleKeyTimes[2] = 1;
    a.rotSpeedMin = -90; a.rotSpeedMax = 90; a.rotStart = 10; a.rotEnd = 20;
    a.force = Vec3(0, -9.8f, 0); a.forceAverage = true;
    a.randomness = 2; a.scope = 0.5f; a.keepVelocity = true;
    a.planePoint = Vec3(0, -1, 0); a.planeNormal = Vec3(0, 0, 1); a.bounce = 0.5f;
    a.colourAdjust1 = Colour(-1, -1, -1, -1); a.colourAdjust2 = Colour(1, 1, 1, 1); a.colourSwitchAt = 0.5f;
    a.colourMin = Colour(-4, -4, -4, -4); a.colourMax = Colour(120, 120, 120, 120);
    a.colourRampPath = "ramp.png";
    a.scaleRate = 1.5f; a.scaleMultiply = true;
    d.affectors = {a};
    return d;
}

}  // namespace

void casesEffects(Fixture &f)
{
    // 1. THE TABLES: inOrder, and the default and a filled value round-trip (a decal's image handles: section 3)
    {
        std::string w1, w2, w3, w4, w5;
        const bool ordered = Record<DecalDesc>::table().inOrder(w1) && Record<ParticleEmitterDesc>::table().inOrder(w2) &&
                             Record<ParticleAffectorDesc>::table().inOrder(w3) && Record<ParticleSystemDesc>::table().inOrder(w4) &&
                             Record<PlanarReflectionParams>::table().inOrder(w5);
        check(ordered, "DecalDesc, ParticleEmitterDesc, ParticleAffectorDesc, ParticleSystemDesc and PlanarReflectionParams' "
                       "tables name every field in declaration order" + (ordered ? std::string() : " - " + w1 + w2 + w3 + w4 + w5));
        DecalDesc dd;
        dd.width = 2; dd.height = 3; dd.depth = 4; dd.metalness = 0.5f; dd.roughness = 0.25f; dd.ignoreAlphaDiffuse = true;
        PlanarReflectionParams pp;
        pp.budget = 3; pp.resolution = 1024; pp.mipmaps = false; pp.shadows = true; pp.hdr = true;
        pp.accurateLighting = false; pp.maxDistance = 5; pp.background = Colour(0.1f, 0.2f, 0.3f, 0.5f);
        check(roundTrips(DecalDesc()) && roundTrips(dd) && roundTrips(ParticleSystemDesc()) && roundTrips(filledSystem()) &&
                  roundTrips(PlanarReflectionParams()) && roundTrips(pp),
              "the effects' records round-trip their defaults and filled values (a particle system's emitters and "
              "affectors, every affector member off its default)");
    }
    if (!f.engine) {
        check(false, "the effects family's cases need the headless engine");
        return;
    }

    // 2. THE DECLARED DOMAINS, refused by name before the body (no engine: a headless scene handed in)
    Scene *real = f.engine->createScene("q-effects-domains");
    const Value scene(f.none.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
    const Value node(f.none.registry().name(kNode, 1));
    {
        const auto call = [&](const char *cmd, Value arg) { return f.none.call(cmd, {scene, node, std::move(arg)}); };
        const auto decal = [&](Value::Object o) { return call("engine.decal.set", obj(std::move(o))); };
        const Value emitter = obj({});
        const auto system = [&](Value::Object o) {
            if (!std::any_of(o.begin(), o.end(), [](const Value::Member &m) { return m.first == "emitters"; }))
                o.emplace_back("emitters", Value(Value::Array{emitter}));
            return call("engine.particles.set", obj(std::move(o)));
        };
        const auto affector = [&](Value::Object a) { return system({{"affectors", Value(Value::Array{obj(std::move(a))})}}); };
        const auto emitterWith = [&](Value::Object e) { return system({{"emitters", Value(Value::Array{obj(std::move(e))})}}); };
        Value::Array seventeen(17, obj({}));
        const auto quads = [&](Value::Object o) { return call("engine.billboards.set", obj(std::move(o))); };
        const auto planar = [&](Value::Object o) { return f.none.call("engine.planar.set", {scene, obj(std::move(o))}); };
        const struct { Result r; const char *text; } refused[] = {
            {decal({{"width", Value(0.0)}}), "decal.width 0 is outside > 0 m"},
            {decal({{"metalness", Value(2.0)}}), "decal.metalness 2 is outside 0..1"},
            {decal({{"roughness", Value(-1.0)}}), "decal.roughness -1 is outside 0..1"},
            {system({{"quota", Value(0)}}), "system.quota 0 is outside 1..16000"},
            {system({{"quota", Value(20000)}}), "system.quota 20000 is outside 1..16000"},
            {system({{"emitters", Value(seventeen)}}), "system.emitters holds 17 elements, at most 16"},
            {system({{"affectors", Value(seventeen)}}), "system.affectors holds 17 elements, at most 16"},
            {system({{"orientation", Value("billboard")}}), "system.orientation 'billboard' is not one of"},
            {emitterWith({{"shape", Value("cone")}}), "system.emitters[0].shape 'cone' is not one of"},
            {emitterWith({{"angleDegrees", Value(200.0)}}), "system.emitters[0].angleDegrees 200 is outside 0..180 degrees"},
            {emitterWith({{"rate", Value(-1.0)}}), "system.emitters[0].rate -1 is outside >= 0 /s"},
            {emitterWith({{"colourStart", nums({200, 0, 0, 1})}}), "system.emitters[0].colourStart[0] 200 is outside -4..120"},
            {emitterWith({{"colourStart", nums({1, 1, 1})}}), "system.emitters[0].colourStart must hold 4 elements (it holds 3)"},
            {affector({{"kind", Value("gravity")}}), "system.affectors[0].kind 'gravity' is not one of"},
            {affector({{"keyCount", Value(7)}}), "system.affectors[0].keyCount 7 is outside 0..6"},
            {affector({{"colourKeyTimes", nums({0, 1.5, 0, 0, 0, 0})}}), "system.affectors[0].colourKeyTimes[1] 1.5 is outside 0..1"},
            {affector({{"scope", Value(2.0)}}), "system.affectors[0].scope 2 is outside 0..1"},
            {call("engine.particles.setColourRamp", obj({{"affector", Value(16)}, {"path", Value("")}})),
             "ramp.affector 16 is outside 0..15"},
            {call("engine.billboards.create", obj({{"capacity", Value(0)}})), "set.capacity 0 is outside 1..65536"},
            {call("engine.billboards.create", obj({{"capacity", Value(8)}, {"layer", Value("hud")}})), "set.layer 'hud' is not one of"},
            {quads({{"positions", nums({0, 0, 0, 1})}}), "quads.positions holds 4 numbers, not a multiple of 3"},
            {quads({{"positions", nums({0, 0, 0})}, {"sizes", nums({-1})}}), "quads.sizes[0] -1 is outside >= 0 m"},
            {planar({{"budget", Value(9)}}), "params.budget 9 is outside 0..8"},
            {planar({{"resolution", Value(32)}}), "params.resolution 32 is outside 64..2048 px"},
            {planar({{"resolution", Value(1000)}}), "params.resolution 1000 is not a power of two"},
            {planar({{"maxDistance", Value(0.0)}}), "params.maxDistance 0 is outside >= 0.01 m"},
            {call("engine.planar.setReflector", obj({})), "reflector.on is required"},
        };
        std::string missed;
        for (const auto &x : refused)
            if (!failedWith(x.r, x.text)) missed += "\n      " + std::string(x.text) + " <- " + x.r.message;
        check(missed.empty(), "the effects commands refuse every declared domain by name (" +
                                  std::to_string(sizeof refused / sizeof refused[0]) + " cases)" + missed);
        const Result ok = planar({{"budget", Value(2)}, {"resolution", Value(1024)}});
        check(ok.status == Status::Failed && ok.message.find("the engine did not start") != std::string::npos,
              "THE CONTROL: planar params inside every bound pass them (" + ok.message + ")");
        // AN ENGINE-WIDE SETTER (one scene per process holds the reflections) is refused for an outside client
        const Value outsideScene(f.outside.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
        const Result outside = f.outside.call("engine.planar.set", {outsideScene, obj({{"budget", Value(1)}})});
        check(failedWith(outside, "engine.planar.set: engine-wide: this host serves an outside client"),
              "engine.planar.set is refused for an outside client (" + outside.message + ")");
    }
    f.engine->destroyScene(real);

    // 3. ON THE HEADLESS ENGINE
    Host &h = f.headless;
    const Value s = h.call("engine.scene.create", {Value("q-effects")}).value;
    Scene *sc = h.registry().object<Scene>(s.asInt());
    const Value cube = h.call("engine.mesh.unitCube", {s}).value;
    const Value mat = h.call("engine.material.createPbr", {s, obj({})}).value;
    const Value n = h.call("engine.node.create", {s}).value;
    h.call("engine.node.attachMesh", {s, n, obj({{"mesh", cube}, {"material", mat}})});
    const char *tga = "q_effects.tga";
    writeTga(tga);
    // the decal: its images by their kind, answered as the same handles
    const Value img = h.call("engine.texture.loadDecal", {s, obj({{"path", Value(tga)}, {"kind", Value("diffuse")}})}).value;
    const Value tex = h.call("engine.texture.create", {s, obj({{"width", Value(1)}, {"height", Value(1)},
                                                              {"rgba", Value::bytes(Value::Bytes(4, 255))}, {"srgb", Value(true)}})}).value;
    h.registry().add({"test.q.decalEcho", {Access::Reads, {target(handle("scene", kScene)), Record<DecalDesc>::table().param("decal")},
                                           Kind::Object},
                      "the decal record read and answered"},
                     [](Call &c) -> Value {
                         DecalDesc d;
                         std::string why;
                         if (!Record<DecalDesc>::table().fromValue(c[1], d, "decal", why)) return c.fail(why);
                         return Record<DecalDesc>::table().toValue(d, &c);
                     });
    const Result echo = h.call("test.q.decalEcho", {s, obj({{"diffuse", img}, {"width", Value(2.0)}})});
    const Result texAsImage = h.call("engine.decal.set", {s, n, obj({{"diffuse", tex}})});
    const Result set = h.call("engine.decal.set", {s, n, obj({{"diffuse", img}, {"width", Value(2.0)}})});
    const Result removed = h.call("engine.decal.remove", {s, n});
    std::printf("Q: the headless engine's decal: set %s, remove %s\n", set.message.c_str(), removed.message.c_str());
    check(echo.status == Status::Ok && echo.value["diffuse"] == img && echo.value["normal"].isNull() &&
              failedWith(texAsImage, "is a texture handle, not a decalTexture handle") && set.value == Value(true) &&
              removed.value == Value(true),
          "engine.decal.set takes decal images (a texture there FAILS by its kind), its record answers the same handle "
          "and none as null; set and remove act on the headless engine (" + texAsImage.message + ")");

    // THE BODIES' OWN REFUSALS (after the node's existence: a live node of the headless engine)
    {
        const Value one(Value::Array{obj({})});
        const struct { Result r; const char *text; } refused[] = {
            {h.call("engine.decal.set", {s, n, obj({})}), "engine.decal.set: decal.diffuse is none: a decal projects its diffuse image"},
            {h.call("engine.particles.set", {s, n, obj({{"emitters", Value(Value::Array{})}})}),
             "engine.particles.set: system.emitters is empty"},
            {h.call("engine.particles.set", {s, n, obj({{"emitters", one},
                                                        {"affectors", Value(Value::Array{obj({{"kind", Value("colourRamp")},
                                                                                              {"colourRampPath", Value("fire.png")}})})}})}),
             "engine.particles.set: system.affectors[0].colourRampPath is a path: engine.particles.setColourRamp sets it"},
            {h.call("engine.billboards.set", {s, n, obj({{"positions", nums({0, 0, 0, 1, 1, 1})}, {"sizes", nums({1})}})}),
             "engine.billboards.set: quads.sizes holds 1 numbers, not 1 x 2 quads"},
        };
        std::string missed;
        for (const auto &x : refused)
            if (!failedWith(x.r, x.text)) missed += "\n      " + std::string(x.text) + " <- " + x.r.message;
        check(missed.empty(), "the effects bodies refuse by name what no declaration can say: a decal with no image, a "
                              "system with no emitter, a ramp's PATH in engine.particles.set (RULED 9), a billboard part's "
                              "length" + missed);
    }

    // the particle system, and RULED 9: the ramp's path by its own files command, on this host's own last record
    const auto ramp = [&](int a, const char *path) {
        return h.call("engine.particles.setColourRamp", {s, n, obj({{"affector", Value(a)}, {"path", Value(path)}})});
    };
    const Result noSystem = ramp(0, tga);
    const Value sys = obj({{"emitters", Value(Value::Array{obj({{"rate", Value(10.0)}})})},
                           {"affectors", Value(Value::Array{obj({{"kind", Value("linearForce")}}), obj({{"kind", Value("colourRamp")}})})}});
    const Result p1 = h.call("engine.particles.set", {s, n, sys});
    std::printf("Q: the headless engine's particles: set %s\n", p1.status == Status::Ok ? "answers" : p1.message.c_str());
    const Result wrongKind = ramp(0, tga), pastEnd = ramp(2, tga), missing = ramp(1, "q_no_such_ramp.png");
    const Result rampSet = ramp(1, tga);
    const auto keptPath = [&]() {
        const ParticleSystemDesc *k = f.live.kept<ParticleSystemDesc>(sc, KeptKind::ParticleSystem, Call::idOf(n));
        return k && k->affectors.size() > 1 ? k->affectors[1].colourRampPath : std::string("(no record)");
    };
    const std::string afterRamp = keptPath();
    const Result p2 = h.call("engine.particles.set", {s, n, sys});   // the ramp it set stays with its affector
    const std::string afterSet = keptPath();
    check(failedWith(noSystem, "carries no particle system this host set") && p1.value == Value(true) &&
              failedWith(wrongKind, "ramp.affector 0 is a \"linearForce\" affector, not a \"colourRamp\"") &&
              failedWith(pastEnd, "ramp.affector 2: the node's system has 2 affectors") &&
              missing.status == Status::Refused && missing.message.find("not there") != std::string::npos &&
              rampSet.value == Value(true) && afterRamp == tga && p2.value == Value(true) && afterSet == tga,
          "RULED 9: setColourRamp FAILS before this host set a system, on a non-ramp affector and past the list, is "
          "REFUSED for a missing image, and sets the ramp on the host's own record (" + afterRamp + "), which a later "
          "engine.particles.set keeps (" + afterSet + ")");
    const Result premoved = h.call("engine.particles.remove", {s, n});
    const Result afterRemove = ramp(1, tga);
    check(premoved.value == Value(true) && failedWith(afterRemove, "carries no particle system this host set"),
          "engine.particles.remove erases the host's record: a later setColourRamp FAILS (" + afterRemove.message + ")");

    // the billboard set
    const Result bc = h.call("engine.billboards.create", {s, n, obj({{"texture", tex}, {"capacity", Value(4)}})});
    const Result bs = h.call("engine.billboards.set", {s, n, obj({{"positions", nums({0, 1, 0, 1, 1, 0})}, {"sizes", nums({0.5, 2})}})});
    std::printf("Q: the headless engine's billboards: create %s, set %s\n", bc.message.c_str(), bs.message.c_str());
    check(bc.value == Value(true) && bs.value == Value(true), "engine.billboards.create and set act on the headless engine");

    // the planar reflections: the scene's (budget 0 here: a budget above 0 makes render targets, which the NULL
    // render system refuses - "Texture 'DepthBuffer_0': Invalid settings!", measured), and a reflector on a mesh that
    // is no plate is the engine's refusal
    const Result pl = h.call("engine.planar.set", {s, obj({{"budget", Value(0)}, {"resolution", Value(256)}})});
    const Result refl = h.call("engine.planar.setReflector", {s, n, obj({{"on", Value(true)}})});
    std::printf("Q: the headless engine's planar: set %s, setReflector %s\n", pl.message.c_str(), refl.message.c_str());
    check(pl.value == Value(true) && refl.status == Status::Refused && refl.message.find("not flat enough") != std::string::npos,
          "engine.planar.set at budget 0 acts on the headless engine (a budget's targets need a device: the train's "
          "card case); setReflector reaches the engine, which REFUSES a mesh that is no plate (" + refl.message + ")");

    // THE OWNERSHIP LAW: in a scene its owner handed in, the owner's node is not the script's to carry an effect
    Scene *handed = f.engine->createScene("q-effects-handed");
    const NodeId ownerNode = handed->createNode();
    {
        Host run;
        run.setConsole([](const std::string &) {});
        makeEngineCommands(run, f.engine, "", false);
        const Value hs(run.registry().hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
        const Result r = run.call("engine.particles.set", {hs, run.registry().name(kNode, ownerNode), sys});
        const Result pr = run.call("engine.planar.set", {hs, obj({})});
        check(failedWith(r, "node " + std::to_string(ownerNode) + " is not this host's to change") &&
                  failedWith(pr, "its settings are not this host's to change"),
              "in a handed-in scene the owner's node carries no effect of the script's, and the scene's planar "
              "reflections are not its to set (" + r.message + ")");
    }
    f.engine->destroyScene(handed);
    std::remove(tga);
}

}  // namespace scriptcommands
