// script.commands - THE GI FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp runs them, in its fixed
// order): per command its declared domain refused by name on the null engine, per table its default and a filled value
// round-trip and inOrder, the test doors (the placement pin, GiParams' test levers) shut without --test-doors and open
// with them, the effect where the headless engine answers (the published update budget: the route judgement's word).
#include "script_commands.h"
#include "script/records_gi.h"
#include "jahshaka/engine/Published.h"

#include <cstdio>
#include <memory>

namespace scriptcommands {

namespace {

Value obj(Value::Object o) { return Value(std::move(o)); }
Value nums(std::initializer_list<double> xs)
{
    Value::Array a;
    for (double x : xs) a.push_back(Value(x));
    return Value(std::move(a));
}

template <class R> bool roundTrips(const R &in)
{
    R back;
    std::string why;
    return Record<R>::table().fromValue(toValue(in), back, "", why) && toValue(back) == toValue(in);
}

GiParams filledGi()
{
    GiParams g;
    g.mode = GiMode::VctPccHybrid;
    g.quality = GiQuality::Epic;
    g.testProbeRegionMin = Vec3(-1, -2, -3);
    g.testProbeRegionMax = Vec3(1, 2, 3);
    g.testProbeGridFitMax = 32;
    g.bounces = false;
    g.pccProbesX = 1; g.pccProbesY = 8; g.pccProbesZ = 4;
    g.probeHdr = GiToggle::On;
    g.probeShadows = GiToggle::Off;
    g.probeCaptureSize = 512;
    g.probeOverlap = 1.5f;
    g.probeSnapDeviation = 0.1f; g.probeSnapSidesMin = 0.5f; g.probeSnapSidesMax = 0.75f;
    g.updateBudget = 3;
    g.ddgi = GiToggle::Auto;
    g.cascadeCount = 2;
    g.cascadeSet[0] = {8.0f, 64, 2.0f};
    g.cascadeSet[1] = {32.0f, 128, 0.0f};
    g.cascadeInstanceCap = 100;
    g.cascadeVoxelLod = false;
    g.dragMoverChannel = true;
    g.cards = false;
    g.cardBudgetTexels = 65536;
    return g;
}

}  // namespace

void casesGi(Fixture &f)
{
    // 1. THE TABLES
    {
        std::string w1, w2, w3, w4;
        const bool ordered = Record<GiParams>::table().inOrder(w1) && Record<GiParams::GiCascadeDesc>::table().inOrder(w2) &&
                             Record<GiPlacementPin>::table().inOrder(w3) && Record<SunContactDesc>::table().inOrder(w4);
        check(ordered, "GiParams (and its cascade), GiPlacementPin and SunContactDesc's tables name every field in declaration "
                       "order" + (ordered ? std::string() : " - " + w1 + w2 + w3 + w4));
        GiPlacementPin pin;
        pin.on = true; pin.cascadeBuiltCam = {Vec3(1, 2, 3), Vec3(4, 5, 6)}; pin.cascadeFree = {0, 1}; pin.field = true;
        pin.fieldOrigin = Vec3(7, 8, 9);
        SunContactDesc sun;
        sun.enabled = true; sun.range = 4; sun.resolution = SunContactResolution::Half;
        GiParams back;
        std::string why;
        const bool whole = Record<GiParams>::table().fromValue(toValue(filledGi()), back, "", why) && back == filledGi() &&
                           back.giTuningEqual(filledGi());
        check(roundTrips(GiParams()) && roundTrips(filledGi()) && whole && roundTrips(GiPlacementPin()) && roundTrips(pin) &&
                  roundTrips(SunContactDesc()) && roundTrips(sun),
              "GiParams (its cascade table whole: cascadeCount and cascadeSet), the pin and the sun contact round-trip their "
              "defaults and filled values (GiParams' operator== and its tuning's agree)");
    }
    if (!f.engine) {
        check(false, "the GI family's cases need the headless engine");
        return;
    }

    // 2. THE DECLARED DOMAINS (no engine: a headless scene handed in) - and the test doors, shut here
    Scene *real = f.engine->createScene("q-gi-domains");
    const Value scene(f.none.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
    Host doors;   // a host started WITH the test doors, on no engine
    doors.setConsole([](const std::string &) {});
    registerEngineCommands(doors, nullptr, "no engine: the test doors open", true);
    const Value doorScene(doors.registry().hold("scene", std::shared_ptr<Scene>(real, [](Scene *) {})));
    {
        const auto gi = [&](Value::Object o) { return f.none.call("engine.gi.set", {scene, obj(std::move(o))}); };
        const auto pin = [&](Value::Object o) { return doors.call("engine.gi.setPlacementPin", {doorScene, obj(std::move(o))}); };
        const auto sun = [&](Value::Object o) { return f.none.call("engine.gi.setSunContact", {scene, obj(std::move(o))}); };
        const struct { Result r; const char *text; } refused[] = {
            {gi({{"mode", Value("rtgi")}}), "params.mode 'rtgi' is not one of"},
            {gi({{"quality", Value("ultra")}}), "params.quality 'ultra' is not one of"},
            {gi({{"pccProbesX", Value(9)}}), "params.pccProbesX 9 is outside 1..8"},
            {gi({{"pccProbesZ", Value(0)}}), "params.pccProbesZ 0 is outside 1..8"},
            {gi({{"probeHdr", Value("yes")}}), "params.probeHdr 'yes' is not one of"},
            {gi({{"probeCaptureSize", Value(2048)}}), "params.probeCaptureSize 2048 is outside 0..1024 px"},
            {gi({{"probeCaptureSize", Value(100)}}), "params.probeCaptureSize 100 is neither 0 nor a power of two in 64..1024"},
            {gi({{"probeCaptureSize", Value(32)}}), "params.probeCaptureSize 32 is neither 0 nor a power of two in 64..1024"},
            {gi({{"probeOverlap", Value(0.0)}}), "params.probeOverlap 0 is outside >= 0.01"},
            {gi({{"probeSnapSidesMax", Value(-1.0)}}), "params.probeSnapSidesMax -1 is outside >= 0"},
            {gi({{"updateBudget", Value(-1)}}), "params.updateBudget -1 is outside 0..2147483647 probes/frame"},
            {gi({{"cascadeInstanceCap", Value(-1)}}), "params.cascadeInstanceCap -1 is outside 0..2147483647"},
            {gi({{"cardBudgetTexels", Value(-1)}}), "params.cardBudgetTexels -1 is outside 0..2147483647 texels/frame"},
            {gi({{"cascadeCount", Value(2)}}), "params has no member 'cascadeCount'"},
            {gi({{"cascadeSet", Value(Value::Array{})}}), "params has no member 'cascadeSet'"},
            {gi({{"testProbeGridFitMax", Value(10.0)}}), "params.testProbeGridFitMax is a test lever (a measuring arm): off its "
                                                          "default only in a host started with --test-doors"},
            {gi({{"testProbeRegionMax", nums({1, 1, 1})}}), "params.testProbeRegionMax is a test lever"},
            {gi({{"cascadeVoxelLod", Value(false)}}), "params.cascadeVoxelLod is a test lever"},
            {f.none.call("engine.gi.setPlacementPin", {scene, obj({{"on", Value(true)}})}),
             "engine.gi.setPlacementPin: a test door: this host was not started with --test-doors"},
            {pin({{"cascadeFree", Value(Value::Array{Value(2)})}}), "pin.cascadeFree[0] 2 is outside 0..1"},
            {pin({{"cascadeBuiltCam", nums({0, 0, 0, 1})}}), "pin.cascadeBuiltCam holds 4 numbers, not a multiple of 3"},
            {pin({{"cascadeBuiltCam", nums({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})}}), "pin.cascadeBuiltCam holds 15 elements, at most 12"},
            {sun({{"range", Value(0.0)}}), "contact.range 0 is outside > 0 m"},
            {sun({{"resolution", Value("quarter")}}), "contact.resolution 'quarter' is not one of"},
        };
        std::string missed;
        for (const auto &x : refused)
            if (!failedWith(x.r, x.text)) missed += "\n      " + std::string(x.text) + " <- " + x.r.message;
        check(missed.empty(), "the GI commands refuse every declared domain by name, the test levers and the pin shut without "
                              "--test-doors (" + std::to_string(sizeof refused / sizeof refused[0]) + " cases)" + missed);
        // THE CONTROLS: inside every bound the call reaches the engine (absent here); the levers and the pin pass with doors
        const Result inside = gi({{"mode", Value("vct")}, {"probeCaptureSize", Value(256)}, {"updateBudget", Value(0)}});
        const Result lever = doors.call("engine.gi.set", {doorScene, obj({{"testProbeGridFitMax", Value(10.0)}, {"cascadeVoxelLod", Value(false)}})});
        const Result pinOpen = pin({{"on", Value(true)}, {"cascadeBuiltCam", nums({1, 2, 3})}, {"cascadeFree", Value(Value::Array{Value(1)})}});
        check(inside.message.find("the engine did not start") != std::string::npos &&
                  lever.message.find("the engine did not start") != std::string::npos &&
                  pinOpen.message.find("the engine did not start") != std::string::npos,
              "THE CONTROLS: GiParams inside every bound pass them (" + inside.message + "); with the test doors the levers "
              "and the pin pass too (" + pinOpen.message + ")");
    }
    f.engine->destroyScene(real);

    // 3. ON THE HEADLESS ENGINE: the GI's parameters published (the update budget: the route judgement's pause), the
    //    relight refused where no voxel chain stands, the sun contact submitted, the pin through a door-open host; in a
    //    scene handed in, the GI is not the script's
    Host &h = f.headless;
    const Value s = h.call("engine.scene.create", {Value("q-gi")}).value;
    Scene *sc = h.registry().object<Scene>(s.asInt());
    const Result set3 = h.call("engine.gi.set", {s, obj({{"mode", Value("vct")}, {"quality", Value("low")}, {"updateBudget", Value(3)}})});
    f.engine->renderOneFrame();
    const int pub3 = sc->published().gi.updateBudget;
    const Result set0 = h.call("engine.gi.set", {s, obj({{"mode", Value("vct")}, {"quality", Value("low")}, {"updateBudget", Value(0)}})});
    f.engine->renderOneFrame();
    const int pub0 = sc->published().gi.updateBudget;
    const Result relight = h.call("engine.gi.refreshLighting", {s});
    const Result contact = h.call("engine.gi.setSunContact", {s, obj({{"enabled", Value(true)}, {"range", Value(4.0)}})});
    std::printf("Q: the headless engine's GI: set %s, the published updateBudget %d then %d, refreshLighting %s, setSunContact %s\n",
                set3.message.c_str(), pub3, pub0, relight.message.c_str(), contact.message.c_str());
    check(set3.value == Value(true) && pub3 == 3 && set0.value == Value(true) && pub0 == 0,
          "engine.gi.set reaches the engine whole: the published update budget reads what the script set (3, then 0: "
          "paused) - THE CONTROL: a value above zero, 3");
    check(relight.status == Status::Refused && relight.message.find("no voxel chain") != std::string::npos &&
              contact.value == Value(true),
          "engine.gi.refreshLighting is REFUSED where no voxel chain stands (the NULL render system builds none: " +
              relight.message + "); engine.gi.setSunContact is submitted for the next frame");
    {
        Host run;
        run.setConsole([](const std::string &) {});
        makeEngineCommands(run, f.engine, "", true);
        const Value own = run.call("engine.scene.create", {Value("q-gi-pin")}).value;
        const Result p = run.call("engine.gi.setPlacementPin", {own, obj({{"on", Value(true)}, {"cascadeBuiltCam", nums({1, 2, 3})}})});
        Scene *handed = f.engine->createScene("q-gi-handed");
        const Value hs(run.registry().hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
        const Result g = run.call("engine.gi.set", {hs, obj({{"mode", Value("vct")}})});
        const Result sunH = run.call("engine.gi.setSunContact", {hs, obj({})});
        check(p.status == Status::Ok && failedWith(g, "its settings are not this host's to change") &&
                  failedWith(sunH, "its settings are not this host's to change"),
              "the pin acts in a door-open host on its own scene (" + p.message + "); a handed-in scene's GI and sun contact "
              "are not the script's (" + g.message + ")");
        f.engine->destroyScene(handed);
    }
}

}  // namespace scriptcommands
