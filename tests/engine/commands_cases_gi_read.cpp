// script.commands - THE GI READERS' CASES (SCRIPT-IN-ENGINE-1 stage 2, builder P; test_script_commands.cpp runs them, in
// its fixed order): the nine `reads` commands - each declared domain refused by name on the null engine, every answer's
// table in order and round-tripped (the big arrays as bytes; the directional levels PACKED exactly as the command's doc
// says), the node handle a card answers minted through its call, the stale / foreign / wrong-kind node handle, and what
// the headless engine answers (no VCT, no surface cache, no ray tier there: each reader refused with its reason - what
// the readers READ is the train's card case).
#include "script_commands.h"
#include "script/records_gi_read.h"

#include <cstring>

namespace scriptcommands {

namespace {

/// A record of T through a registry's check and back (C++ -> value -> the registry's conformance -> C++ -> value):
/// equal to the value it started from, or `why` says the first fault.
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

template <class T> void inOrder(const char *name, std::string &missed)
{
    std::string why;
    if (!Record<T>::table().inOrder(why)) missed += std::string("\n      ") + name + ": " + why;
}

Value nums(std::initializer_list<double> v)
{
    Value::Array a;
    for (const double x : v) a.push_back(Value(x));
    return Value(std::move(a));
}

}  // namespace

void casesGiRead(Fixture &f)
{
    if (!f.engine) {
        check(false, "THE GI READERS: the headless engine did not boot - their cases are skipped");
        return;
    }
    const Value scene = f.headless.call("engine.scene.create", {Value("p-gi-read")}).value;
    const Value node = f.headless.call("engine.node.create", {scene}).value;
    Scene *raw = f.headless.registry().object<Scene>(scene.asInt());
    const std::int64_t noneScene = f.none.registry().hold<Scene>(kScene, std::shared_ptr<Scene>(raw, [](Scene *) {}));
    const Value ns(noneScene);
    const Value at(Value::Object{{"world", nums({0, 0, 0})}, {"normal", nums({0, 1, 0})}});

    // 1. THE DECLARED DOMAIN on the null engine, each FAILS by name before the engine is asked; THE CONTROL: arguments
    //    in their domain reach the body (no engine there)
    {
        Value::Array tooMany(12 * kGiReadRays + 12, Value(0.0));
        const struct { const char *command; Value arg; const char *want; } cases[] = {
            {"engine.gi.voxelStats", Value(4), "cascade 4 is outside 0..3"},
            {"engine.gi.voxelVolume", Value(-1), "cascade -1 is outside 0..3"},
            {"engine.gi.voxelDirect", Value(1.5), "cascade must be an integer (1.5 is not; it is never truncated)"},
            {"engine.gi.voxelLight", Value(Value::Object{{"cascade", Value(0)}, {"lo", nums({0, 0, 0})}}),
             "read.lo and read.hi are both given or neither"},
            {"engine.gi.voxelHalves", Value(Value::Object{{"cascade", Value(0)}, {"lo", nums({0, 1, 0})}, {"hi", nums({1, 1, 1})}}),
             "read.lo[1] is not below read.hi[1]: an empty box"},
            {"engine.gi.voxelLight", Value(Value::Object{{"cascade", Value(9)}}), "read.cascade 9 is outside 0..3"},
            {"engine.gi.readCardAt", Value(Value::Object{{"world", nums({0, 0})}, {"normal", nums({0, 1, 0})}}),
             "at.world must hold 3 elements (it holds 2)"},
            {"engine.gi.readCardAt", Value(Value::Object{{"normal", nums({0, 1, 0})}}), "at.world is required"},
            {"engine.gi.traceRays", Value(Value::Object{{"rays", Value(Value::Array(13, Value(0.0)))}}),
             "trace.rays holds 13 numbers, not a multiple of 12"},
            {"engine.gi.traceRays", Value(Value::Object{{"rays", Value(std::move(tooMany))}}),
             "trace.rays holds 786444 elements, at most 786432"},
        };
        std::string missed;
        for (const auto &d : cases) {
            const Result r = f.none.call(d.command, {ns, d.arg});
            if (!failedWith(r, std::string(d.command) + ": " + d.want))
                missed += std::string("\n      ") + d.command + ": want '" + d.want + "' <- " + r.message;
        }
        std::string reached;
        for (const auto &[command, arg] : std::initializer_list<std::pair<const char *, Value>>{
                 {"engine.gi.voxelStats", Value(3)},
                 {"engine.gi.voxelLight", Value(Value::Object{{"cascade", Value(0)}, {"lo", nums({0, 0, 0})}, {"hi", nums({1, 1, 1})}})},
                 {"engine.gi.readCardAt", at},
                 {"engine.gi.traceRays", Value(Value::Object{{"rays", Value(Value::Array(12, Value(1.0)))}})}}) {
            const Result r = f.none.call(command, {ns, arg});
            if (!failedWith(r, "the engine did not start")) reached += std::string("\n      ") + command + ": " + r.message;
        }
        for (const char *command : {"engine.gi.readCardRadiance", "engine.gi.changedRegions"}) {
            const Result r = f.none.call(command, {ns});
            if (!failedWith(r, "the engine did not start")) reached += std::string("\n      ") + command + ": " + r.message;
        }
        check(missed.empty() && reached.empty(),
              "THE GI READERS' DECLARED DOMAIN, each refused by name on the null engine (" + std::to_string(std::size(cases)) +
                  " cases: the cascade 0..3, the box's corners, the point's vectors, a ray batch's stride and its 65,536 "
                  "rays); THE CONTROL: the nine readers in their domain reach the body (no engine there)" + missed + reached);
    }

    // 2. THE ANSWERS' TABLES: every record in declaration order; the default and a record with every field changed
    //    round-trip (the big arrays as bytes); THE PACKED LAYOUT of the directional levels is the doc's, byte for byte
    {
        std::string missed;
        inOrder<CardSample>("CardSample", missed);
        inOrder<CardRadiance>("CardRadiance", missed);
        inOrder<GiVoxelStats>("GiVoxelStats", missed);
        inOrder<GiVoxelVolume>("GiVoxelVolume", missed);
        inOrder<GiChangedRegion>("GiChangedRegion", missed);
        inOrder<GiChangedRegions>("GiChangedRegions", missed);
        CardSample sample;
        sample.ok = true; sample.albedo[0] = 0.5f; sample.normal[1] = 1; sample.emissive[2] = 2; sample.depth = 3;
        sample.shadow = 1; sample.roughness = 0.25f; sample.radiance[0] = 4; sample.indirect[1] = 5;
        sample.indirectStored[2] = 6; sample.specularF0[0] = 0.04f; sample.specularWeight = 0.5f; sample.flat[1] = 7;
        sample.lampMask = 0x5ull; sample.lampOverflowVis = 0.5f; sample.lampThroughMask = 0x2ull; sample.lampThroughVis = 0.25f;
        sample.lampIds[0] = 11; sample.lampIds[47] = 12; sample.lampCount = 2; sample.card = 3; sample.texelX = 4;
        sample.texelY = 5; sample.lit = true; sample.position[0] = 1; sample.worldNormal[2] = -1; sample.sunLift = 0.01f;
        sample.gather[3] = 8; sample.gatherSky = 0;
        CardRadiance card;
        card.card = 2; card.bucket = 5; card.size = 2; card.lastRelit = 10; card.lastIndirect = 11; card.lastRead = 12;
        card.rgb = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        GiVoxelStats stats;
        stats.available = true; stats.cascade = 1; stats.width = 2; stats.height = 3; stats.depth = 4;
        stats.format = "PFG_RGBA16_FLOAT"; stats.formatMax = 0; stats.peak = 9; stats.peakDirect = 8; stats.meanLit = 0.5;
        stats.voxelsLit = 7; stats.voxelsAtMax = 1; stats.directAtMax = 2; stats.voxels = 24; stats.voxelsAboveOne = 3;
        stats.emissiveFormat = "PFG_RGBA8_UNORM"; stats.peakEmissive = 1; stats.emissiveAtMax = 4; stats.emissiveAboveOne = 0;
        stats.lightDigest = "0123456789abcdef"; stats.storeDigest = "fedcba9876543210"; stats.directDigest = "00000000ffffffff";
        GiVoxelVolume volume;
        volume.available = true; volume.width = 1; volume.height = 1; volume.depth = 1;
        volume.origin[0] = -1; volume.cell[1] = 0.5f;
        for (auto *a : {&volume.light, &volume.albedo, &volume.coverageP, &volume.coverageN, &volume.positionP, &volume.positionN,
                        &volume.emissive, &volume.emitP, &volume.emitN, &volume.direct, &volume.halfLight, &volume.halfDirect})
            *a = {0.25f, 0.5f, 0.75f, 1.0f};
        volume.accumWeight = {42};
        volume.albedoLevels = {{1, 2, 3, 4}, {5, 6, 7, 8}};
        volume.directional[0] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
        volume.directional[2] = {{9, 10, 11, 12}};
        volume.halfSide[1] = {0.5f, 0.5f, 0.5f, 0.5f};
        volume.halfNormal[2] = {0.5f, 0.5f, 0.5f, 0.5f};
        GiChangedRegions regions;
        regions.available = true; regions.frame = 99; regions.overflow = kGiRegionOverflowSun; regions.restart = true;
        regions.sun = true; regions.toSun = Vec3(0, 1, 0); regions.merges = 3;
        regions.regions = {GiChangedRegion{Vec3(0, 0, 0), Vec3(1, 1, 1), GiRegionKind::Lamp, 1u},
                           GiChangedRegion{Vec3(-1, -1, -1), Vec3(0, 0, 0), GiRegionKind::MoverShadow, 3u}};
        std::string why;
        const auto trip = [&](bool ok, const char *name) { if (!ok) missed += std::string("\n      ") + name + ": " + why; };
        trip(roundTrips(CardSample{}, why), "CardSample default");
        trip(roundTrips(sample, why), "CardSample filled");
        trip(roundTrips(CardRadiance{}, why), "CardRadiance default");
        trip(roundTrips(card, why), "CardRadiance filled");
        trip(roundTrips(GiVoxelStats{}, why), "GiVoxelStats default");
        trip(roundTrips(stats, why), "GiVoxelStats filled");
        trip(roundTrips(GiVoxelVolume{}, why), "GiVoxelVolume default");
        trip(roundTrips(volume, why), "GiVoxelVolume filled");
        trip(roundTrips(GiChangedRegions{}, why), "GiChangedRegions default");
        trip(roundTrips(regions, why), "GiChangedRegions filled");
        // the doc's packed layout: u32 n, n u32 counts, the levels' floats in order (this machine's byte order)
        const Value answered = Record<GiVoxelVolume>::table().toValue(volume);
        Value::Bytes want(4 * 3 + 8 * 4);
        const std::uint32_t head[3] = {2, 4, 4};
        const float body[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        std::memcpy(want.data(), head, sizeof head);
        std::memcpy(want.data() + sizeof head, body, sizeof body);
        const bool packed = answered["directional"].asArray()[0] == Value::bytes(want) &&
                            answered["directional"].asArray()[1] == Value::bytes(Value::Bytes(4, 0)) &&
                            answered["light"].kind() == Kind::Bytes && answered["light"].asBytes().size() == 16 &&
                            answered["albedoLevels"].asArray().size() == 2;
        if (!packed) missed += "\n      the directional levels are not packed as the doc says (u32 n, n u32 counts, the floats)";
        check(missed.empty(), "THE GI READERS' ANSWERS: CardSample, CardRadiance, GiVoxelStats, GiVoxelVolume, "
                              "GiChangedRegion and GiChangedRegions in declaration order; each default and filled record "
                              "round-trips through the registry; a volume's arrays answer as bytes and its directional "
                              "levels PACKED exactly as engine.gi.voxelVolume's doc says (an empty axis: one u32 0)" + missed);
    }

    // 3. A CARD's NODE IS A NODE HANDLE: answered through its call, minted as this host's handle of the node (0: null)
    {
        f.headless.registry().add({"test.p.card", {Access::Test, {arg("node", Kind::Int)}, Kind::Object}, "a card of the node"},
                                  [](Call &c) -> Value {
                                      CardRadiance r;
                                      r.node = NodeId(c[0].asInt());
                                      return Record<CardRadiance>::table().toValue(r, &c);
                                  });
        f.headless.registry().openTestDoors(true);
        const Result of = f.headless.call("test.p.card", {Value(std::int64_t(Call::idOf(node)))});
        const Result none = f.headless.call("test.p.card", {Value(0)});
        f.headless.registry().openTestDoors(false);
        check(of.status == Status::Ok && of.value["node"] == node && none.value["node"].isNull(),
              "A CARD's NODE answers as this host's node handle (the same handle engine.node.create gave), node 0 as null");
    }

    // 4. ON THE HEADLESS ENGINE (no VCT, no surface cache, no ray tier): each reader REFUSED with its reason (null), the
    //    voxel stats answered unavailable, the changed regions refused even after a frame (the NULL engine closes no
    //    list: measured)
    {
        const Result frame = f.headless.call("engine.frames", {Value(1)});
        std::string missed;
        const auto refused = [&](const char *command, Value::Array args, const char *reason) {
            const Result r = f.headless.call(command, args);
            if (r.status != Status::Refused || !r.value.isNull() || r.message.find(reason) == std::string::npos)
                missed += std::string("\n      ") + command + ": " + r.message;
        };
        refused("engine.gi.readCardAt", {scene, at}, "no card covers the point");
        refused("engine.gi.readCardRadiance", {scene}, "the scene keeps no surface cache");
        refused("engine.gi.voxelVolume", {scene, Value(0)}, "no voxel cascade there");
        refused("engine.gi.voxelLight", {scene, Value(Value::Object{{"cascade", Value(0)}})}, "no voxel cascade there");
        refused("engine.gi.voxelDirect", {scene, Value(0)}, "no direct term there");
        refused("engine.gi.voxelHalves", {scene, Value(Value::Object{{"cascade", Value(0)}, {"lo", nums({0, 0, 0})}, {"hi", nums({1, 1, 1})}})},
                "no voxel cascade there");
        refused("engine.gi.traceRays", {scene, Value(Value::Object{{"rays", Value(Value::Array(12, Value(1.0)))}})}, "no ray was traced");
        const Result regions = f.headless.call("engine.gi.changedRegions", {scene});
        const Result stats = f.headless.call("engine.gi.voxelStats", {scene, Value(0)});
        check(missed.empty() && frame.status == Status::Ok && stats.status == Status::Ok &&
                  stats.value["available"] == Value(false) && regions.status == Status::Refused && regions.value.isNull(),
              "ON THE HEADLESS ENGINE the GI readers answer what it has: seven refused with their reason, the voxel stats "
              "unavailable, the changed regions refused even after a frame (measured: the NULL engine closes no list - " +
                  regions.message + "); what they READ is the train's card case" + missed);
    }

    // 5. readCardAt's NODE: a destroyed node, another host's node handle, a mesh given as a node each FAIL by name
    {
        const Value dead = f.headless.call("engine.node.create", {scene}).value;
        f.headless.call("engine.node.destroy", {scene, dead});
        const Value mesh = f.headless.call("engine.mesh.unitCube", {scene}).value;
        const Value foreign(f.none.registry().name(kNode, Call::idOf(node)));
        const auto read = [&](Value n) {
            return f.headless.call("engine.gi.readCardAt",
                                   {scene, Value(Value::Object{{"world", nums({0, 0, 0})}, {"normal", nums({0, 1, 0})}, {"node", n}})});
        };
        const Result stale = read(dead), other = read(foreign), kind = read(mesh), all = read(Value(0));
        check(failedWith(stale, "is not a node of scene 'p-gi-read'") && failedWith(other, "is no node handle of this host") &&
                  failedWith(kind, "is a mesh handle, not a node handle") && all.status == Status::Refused,
              "readCardAt's NODE: a destroyed node (" + stale.message + "), another host's (" + other.message + "), a mesh as "
              "a node (" + kind.message + ") each FAIL by name; THE CONTROL: node 0 (every card) reaches the engine (" +
                  all.message + ")");
        f.headless.call("engine.mesh.destroy", {scene, mesh});
    }
    f.none.registry().release(noneScene);
    f.headless.call("engine.scene.destroy", {scene});
}

}  // namespace scriptcommands
