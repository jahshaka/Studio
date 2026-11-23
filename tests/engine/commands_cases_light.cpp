// script.commands - THE LIGHT FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2, builder P; test_script_commands.cpp runs them,
// in its fixed order): per command its declared domain refused by name on the null engine, LightDesc's table (its
// default and a filled value round-trip and inOrder), the least privilege of the IES profile's path (RULED 9) and the
// host's kept record, the engine-wide setter refused for an outside client, and the effect on the headless engine.
#include "script_commands.h"
#include "script/records_light.h"

namespace scriptcommands {

namespace {

/// A record of T through a registry's check and back: C++ -> value -> the registry's conformance (its kinds, ranges,
/// counts) -> C++ -> value. Equal to the value it started from, or `why` says the first fault.
template <class T> bool roundTrips(const T &record, std::string &why)
{
    Registry reg;
    T seen{};
    const Value sent = Record<T>::table().toValue(record);
    reg.add({"test.record", {Access::Reads, {Record<T>::table().param("r")}, Kind::Object}, "the record, through C++"},
            [&seen](Call &c) -> Value {
                std::string w;
                if (!Record<T>::table().fromValue(c[0], seen, "r", w)) return c.fail(w);
                return Record<T>::table().toValue(seen);
            });
    const Result r = reg.call("test.record", {sent});
    if (!reg.validate().empty()) why = reg.validate()[0];
    else if (r.status != Status::Ok) why = r.message;
    else if (r.value != sent) why = "the value came back changed";
    else return true;
    return false;
}

}  // namespace

void casesLight(Fixture &f)
{
    if (!f.engine) {
        check(false, "THE LIGHT FAMILY: the headless engine did not boot - its cases are skipped");
        return;
    }
    // THE SUBJECTS: a scene and a node the headless host made; the same scene HELD UNOWNED by the host on no engine and
    // the node named there - so the null engine's call reaches its argument laws (its body then FAILS: no engine)
    const Value scene = f.headless.call("engine.scene.create", {Value("p-light")}).value;
    const Value node = f.headless.call("engine.node.create", {scene}).value;
    Scene *raw = f.headless.registry().object<Scene>(scene.asInt());
    const std::int64_t noneScene = f.none.registry().hold<Scene>(kScene, std::shared_ptr<Scene>(raw, [](Scene *) {}));
    const Value noneNode(f.none.registry().name(kNode, Call::idOf(node)));
    check(raw && noneScene && !noneNode.isNull() && noneNode.asInt() != 0,
          "THE LIGHT FAMILY's subjects: a headless scene and node, held unowned on the host with no engine");

    // 1. THE DECLARED DOMAIN, refused by name on the null engine (the arguments are checked before the body): every
    //    LightDesc field's own range (the engine's clamps), a colour of three numbers, the IES path, the shadow side
    const auto light = [&](Value::Object o) {
        return f.none.call("engine.light.set", {Value(noneScene), noneNode, Value(std::move(o))});
    };
    const Value rgb3(Value::Array{Value(1.0), Value(1.0), Value(1.0)});
    const struct { Value::Member member; const char *want; } domains[] = {
        {{"type", Value("laser")}, "light.type 'laser' is not one of \"directional\"|\"point\"|\"spot\"|\"area\""},
        {{"colour", rgb3}, "light.colour must hold 4 elements (it holds 3)"},
        {{"colour", Value(Value::Array{Value(1.0), Value(-0.5), Value(1.0), Value(1.0)})}, "light.colour[1] -0.5 is outside >= 0"},
        {{"intensity", Value(-1.0)}, "light.intensity -1 is outside >= 0"},
        {{"range", Value(0.001)}, "light.range 0.001 is outside >= 0.01 m"},
        {{"sourceRadius", Value(0.0)}, "light.sourceRadius 0 is outside >= 0.001 m"},
        {{"spotAngleDegrees", Value(90.0)}, "light.spotAngleDegrees 90 is outside 1..85 degrees"},
        {{"spotSoftness", Value(1.0)}, "light.spotSoftness 1 is outside 0..0.99"},
        {{"spotFalloff", Value(-1.0)}, "light.spotFalloff -1 is outside >= 0"},
        {{"rectWidth", Value(0.0)}, "light.rectWidth 0 is outside >= 0.01 m"},
        {{"rectHeight", Value(0.0)}, "light.rectHeight 0 is outside >= 0.01 m"},
        {{"lightMask", Value(-1)}, "light.lightMask -1 is outside 0..4294967295"},
        {{"castShadows", Value(1)}, "light.castShadows must be bool (it is int)"},
        {{"wattage", Value(60)}, "light has no member 'wattage'"},
    };
    std::string missed, colourText;
    for (const auto &d : domains) {
        const Result r = light({d.member});
        if (d.member.first == "colour" && colourText.empty()) colourText = r.message;
        if (!failedWith(r, std::string("engine.light.set: ") + d.want))
            missed += "\n      " + d.member.first + ": want '" + d.want + "' <- " + r.message;
    }
    const Result noPath = f.none.call("engine.light.setIesProfile", {Value(noneScene), noneNode, Value(Value::Object{})});
    const Result small = f.none.call("engine.light.setShadowResolution", {Value(100)});
    const Result huge = f.none.call("engine.light.setShadowResolution", {Value(16384)});
    if (!failedWith(noPath, "engine.light.setIesProfile: profile.path is required")) missed += "\n      path: " + noPath.message;
    if (!failedWith(small, "engine.light.setShadowResolution: pixels 100 is outside 256..8192 px")) missed += "\n      100: " + small.message;
    if (!failedWith(huge, "pixels 16384 is outside 256..8192 px")) missed += "\n      16384: " + huge.message;
    // THE CONTROL: a light in its domain passes the arguments' laws; the node's existence, asked of no engine, then FAILS
    const Result inDomain = light({{"type", Value("spot")}, {"spotAngleDegrees", Value(85.0)}});
    check(missed.empty() && failedWith(inDomain, "the node's scene is gone"),
          "THE LIGHT's DECLARED DOMAIN, each refused by name on the null engine (" + std::to_string(std::size(domains) + 3) +
              " cases; a 3-number colour: '" + colourText + "'); THE CONTROL: a light in its domain passes them (the "
              "node's existence, asked of no engine, then FAILS: " + inDomain.message + ")" + missed);

    // 2. ENGINE-WIDE: the shadow side is refused for an outside client, before its engine is asked
    const Result outside = f.outside.call("engine.light.setShadowResolution", {Value(2048)});
    check(failedWith(outside, "engine.light.setShadowResolution: engine-wide: this host serves an outside client") &&
              failedWith(f.none.call("engine.light.setShadowResolution", {Value(2048)}), "the engine did not start"),
          "engine.light.setShadowResolution is ENGINE-WIDE: an outside client's host refuses it (" + outside.message +
              "); THE CONTROL: a host for no outside client reaches the engine");

    // 3. THE TABLE: LightDesc's fields in order, its default and a record with every field changed round-trip
    {
        std::string why, whyDefault, whyFilled;
        const bool ordered = Record<LightDesc>::table().inOrder(why);
        LightDesc filled;
        filled.type = LightType::Spot;
        filled.colour = Colour(0.25f, 0.5f, 0.75f, 0.5f);
        filled.intensity = 3.5f;
        filled.range = 12.0f;
        filled.sourceRadius = 0.25f;
        filled.spotAngleDegrees = 40.0f;
        filled.spotSoftness = 0.5f;
        filled.spotFalloff = 2.0f;
        filled.castShadows = false;
        filled.rectWidth = 2.0f;
        filled.rectHeight = 3.0f;
        filled.doubleSided = true;
        filled.iesProfilePath = "/profiles/a.ies";
        filled.lightMask = 0x0000F00Fu;
        filled.forwardShadingPriority = 3;
        filled.primaryDirectional = false;
        const bool def = roundTrips(LightDesc{}, whyDefault), full = roundTrips(filled, whyFilled);
        check(ordered && def && full && Record<LightDesc>::table().fields().size() == 16,
              "LightDesc's TABLE: 16 fields in declaration order (" + why + "); its default and a record with every "
              "field changed round-trip through the registry" + (def ? "" : " - default: " + whyDefault) +
                  (full ? "" : " - filled: " + whyFilled));
    }

    // 4. ON THE HEADLESS ENGINE: LEAST PRIVILEGE (RULED 9) - a path given to the `changes` command FAILS by name (no
    //    light made); a light set, its profile set by the `files` command and KEPT by the next set, a light removed
    //    takes the host's record with it (the profile's command then FAILS, a second remove is refused)
    {
        const Result pathed =
            f.headless.call("engine.light.set", {scene, node, Value(Value::Object{{"iesProfilePath", Value("/a.ies")}})});
        const bool noLight = f.live.kept<LightDesc>(raw, KeptKind::Light, Call::idOf(node)) == nullptr;
        check(failedWith(pathed, "engine.light.set: light.iesProfilePath: a file path is set by engine.light.setIesProfile "
                                 "(a `files` command), never by engine.light.set") && noLight,
              "LEAST PRIVILEGE FOR PATHS: engine.light.set FAILS on a non-empty IES path by name (" + pathed.message +
                  "), no light kept");
        const Result set = f.headless.call("engine.light.set", {scene, node, Value(Value::Object{{"type", Value("spot")}})});
        const Result profiled =
            f.headless.call("engine.light.setIesProfile", {scene, node, Value(Value::Object{{"path", Value("/no/such.ies")}})});
        const Result again = f.headless.call("engine.light.set", {scene, node, Value(Value::Object{{"intensity", Value(2.0)}})});
        const LightDesc *kept = f.live.kept<LightDesc>(raw, KeptKind::Light, Call::idOf(node));
        const bool keptPath = kept && kept->iesProfilePath == "/no/such.ies" && kept->intensity == 2.0f;
        const Result removed = f.headless.call("engine.light.remove", {scene, node});
        const bool gone = f.live.kept<LightDesc>(raw, KeptKind::Light, Call::idOf(node)) == nullptr;
        const Result after =
            f.headless.call("engine.light.setIesProfile", {scene, node, Value(Value::Object{{"path", Value("")}})});
        const Result twice = f.headless.call("engine.light.remove", {scene, node});
        const Result side = f.headless.call("engine.light.setShadowResolution", {Value(2048)});
        check(set.status == Status::Ok && set.value == Value(true) && profiled.status == Status::Ok && again.status == Status::Ok &&
                  keptPath && removed.value == Value(true) && gone &&
                  failedWith(after, "this host set no light on it (engine.light.set first)") &&
                  twice.status == Status::Refused && twice.value == Value(false) &&
                  twice.message.find("carries no light") != std::string::npos && side.status == Status::Ok,
              "ON THE HEADLESS ENGINE a light answers: set (" + set.message + "), its IES path set by setIesProfile and KEPT "
              "by the next engine.light.set (the host's record: '" + (kept ? kept->iesProfilePath : std::string("none")) +
                  "'), removed with the record (then setIesProfile FAILS: " + after.message +
                  "; a second remove is refused: " + twice.message + "); the shadow side set");
    }

    // 5. THE HANDLES: a destroyed node, another host's node handle and a handle of the wrong kind each FAIL by name;
    //    THE CONTROL: a live node of the scene passes (above)
    {
        const Value dead = f.headless.call("engine.node.create", {scene}).value;
        f.headless.call("engine.node.destroy", {scene, dead});
        const Value mesh = f.headless.call("engine.mesh.unitCube", {scene}).value;
        const Result stale = f.headless.call("engine.light.set", {scene, dead});
        const Result foreign = f.headless.call("engine.light.set", {scene, noneNode});
        const Result kind = f.headless.call("engine.light.remove", {scene, mesh});
        check(failedWith(stale, "is not a node of scene 'p-light'") && failedWith(foreign, "is no node handle of this host") &&
                  failedWith(kind, "is a mesh handle, not a node handle"),
              "A LIGHT's NODE HANDLE: a destroyed node (" + stale.message + "), another host's node (" + foreign.message +
                  ") and a mesh given as a node (" + kind.message + ") each FAIL by name");
        f.headless.call("engine.mesh.destroy", {scene, mesh});
    }
    f.none.registry().release(noneScene);
    f.headless.call("engine.scene.destroy", {scene});
}

}  // namespace scriptcommands
