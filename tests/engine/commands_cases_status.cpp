// script.commands - THE STATUS FAMILY's CASES (SCRIPT-IN-ENGINE-1 2d; test_script_commands.cpp runs them, in its fixed
// order). THE TABLES (records_status.h: every record the status commands answer, each nested one with its own table):
// each in declaration order (Table::inOrder), its default round-tripping, and a value with EVERY LEAF MOVED round-tripping
// - moved generically, guided by the table's own parameter (a number +0.5 or +1 inside its type's range, a bool flipped,
// a string set, an enum to another of its names, an absent record or vector given one moved element, a fixed array's
// every element moved), so no field of the 1,100 is left at a default the round trip would pass by luck: the moved leaves
// are counted and printed. THE COMMANDS: on the host with no engine each reaches its body (the engine
// absent, by name) after its arguments' laws; engine.monitor.set's level refused by name and the setter refused for an
// outside client (ENGINE-WIDE); on the headless engine the engine's published frame, the submit status, the shader
// cache's figures and the monitor's queues answer, the published frame round-tripping AS THE ENGINE WROTE IT. A view's
// row and its exposure need a view (the headless engine makes none): card cases of the train.
#include "script_commands.h"
#include "script/records_status.h"

#include <functional>

namespace scriptcommands {

namespace {

/// The default of a parameter's ELEMENT or of an absent record: an object of its members' defaults.
Value objectDefault(const Param &p)
{
    Value::Object o;
    for (const Param &f : p.fields) o.emplace_back(f.name, f.fallback ? *f.fallback : Value());
    return Value(std::move(o));
}

/// `v` (conformed to `p`) with every leaf MOVED off its value; `moved` counts the leaves moved, `kept` those it cannot
/// move by shape alone (an array of byte arrays: its elements' sizes are the record's, not the parameter's).
Value moveAll(const Param &p, const Value &v, std::size_t &moved, std::size_t &kept)
{
    switch (p.kind) {
    case Kind::Bool: ++moved; return Value(!v.asBool());
    case Kind::Int: {
        ++moved;
        const std::int64_t x = v.asInt();
        const bool roomUp = !p.max || double(x + 1) <= *p.max;
        return Value(roomUp ? x + 1 : x - 1);
    }
    case Kind::Number: {   // (a float field moves to a float: its round trip is exact)
        ++moved;
        const double x = v.asNumber() + 0.5;
        return Value(p.single ? double(float(x)) : x);
    }
    case Kind::String:
        ++moved;
        if (!p.choices.empty()) return Value(p.choices[0] == v.asString() ? p.choices.back() : p.choices[0]);
        return Value(v.asString() + "x");
    case Kind::Bytes: {
        ++moved;
        Value::Bytes b = v.asBytes();
        if (b.empty()) b.push_back(1); else b[0] ^= 0x5a;
        return Value::bytes(std::move(b));
    }
    case Kind::Object: {
        const Value base = v.isNull() ? objectDefault(p) : v;
        Value::Object o;
        for (const Param &f : p.fields) {
            const Value *m = base.find(f.name);
            o.emplace_back(f.name, moveAll(f, m ? *m : Value(), moved, kept));
        }
        return Value(std::move(o));
    }
    case Kind::Array: {
        Value::Array a = v.isNull() ? Value::Array() : v.asArray();
        if (p.element == Kind::Bytes) {   // an array of byte arrays: kept as it is
            ++kept;
            return Value(std::move(a));
        }
        Param e = p;   // the element's parameter: its kind, fields, choices and range
        e.kind = p.element;
        e.count = 0;
        e.stride = 0;
        if (a.empty()) {   // a vector at its default (empty): one element, at the element's default
            if (p.element == Kind::Object) a.push_back(objectDefault(p));
            else for (unsigned i = 0; i < (p.stride ? p.stride : 1u); ++i) a.push_back(p.element == Kind::Bool ? Value(false)
                     : p.element == Kind::String ? Value(p.choices.empty() ? std::string() : p.choices[0])
                     : p.element == Kind::Int ? Value(0) : Value(0.0));
        }
        for (Value &x : a) x = moveAll(e, x, moved, kept);
        return Value(std::move(a));
    }
    default: ++kept; return v;
    }
}

/// THE TABLE's LAWS: in order; its default round-trips; every leaf moved and round-tripping (the leaves counted).
template <class R> void tableLaws(const char *name)
{
    const Table<R> &t = Record<R>::table();
    std::string order, why;
    const bool inOrder = t.inOrder(order);
    R def{};
    const Value dv = toValue(def);
    R back{};
    const bool dRound = t.fromValue(dv, back, "", why) && toValue(back) == dv;
    std::size_t moved = 0, kept = 0;
    const Value mv = moveAll(t.param(name), dv, moved, kept);
    R filled{};
    const bool read = t.fromValue(mv, filled, "", why);
    const Value again = read ? toValue(filled) : Value();
    check(inOrder && dRound && read && again == mv && mv != dv && moved > 0,
          std::string("THE TABLE ") + name + ": in declaration order" + (order.empty() ? "" : " (" + order + ")") +
              ", its default round-trips, and with every leaf moved (" + std::to_string(moved) + " moved, " +
              std::to_string(kept) + " byte-array leaves kept) it round-trips" + (why.empty() ? "" : " - " + why));
}

}  // namespace

void casesStatus(Fixture &f)
{
    // 1. THE TABLES: every record the status commands answer (the nested ones are walked through their outer records'
    //    leaves, and judged in order on their own)
    tableLaws<PublishedFrame>("PublishedFrame");
    tableLaws<PublishedFrame::RiderTag>("PublishedFrame::RiderTag");
    tableLaws<PublishedFrame::Facts>("PublishedFrame::Facts");
    tableLaws<PublishedFrame::Facts::Share>("PublishedFrame::Facts::Share");
    tableLaws<PublishedFrame::Facts::NodeClips>("PublishedFrame::Facts::NodeClips");
    tableLaws<PublishedFrame::Facts::Clip>("PublishedFrame::Facts::Clip");
    tableLaws<PublishedFrame::Completion>("PublishedFrame::Completion");
    tableLaws<PublishedFrame::Atmosphere>("PublishedFrame::Atmosphere");
    tableLaws<PublishedFrame::Completeness>("PublishedFrame::Completeness");
    tableLaws<PublishedFrame::AtomHidden>("PublishedFrame::AtomHidden");
    tableLaws<PublishedFrame::NodeCount>("PublishedFrame::NodeCount");
    tableLaws<PublishedFrame::RigPoses>("PublishedFrame::RigPoses");
    tableLaws<PublishedFrame::NodePose>("PublishedFrame::NodePose");
    tableLaws<ViewFrames>("ViewFrames");
    tableLaws<PublishedEngineFrame>("PublishedEngineFrame");
    tableLaws<PublishedEngineFrame::FrameError>("PublishedEngineFrame::FrameError");
    tableLaws<PublishedEngineFrame::Refusal>("PublishedEngineFrame::Refusal");
    tableLaws<GiStatus>("GiStatus");
    tableLaws<GiStatus::CascadeStatus>("GiStatus::CascadeStatus");
    tableLaws<GatherStatus>("GatherStatus");
    tableLaws<GatherRestRecord>("GatherRestRecord");
    tableLaws<GatherRay>("GatherRay");
    tableLaws<CardCacheStatus>("CardCacheStatus");
    tableLaws<CardCacheStatus::Refused>("CardCacheStatus::Refused");
    tableLaws<CardCacheStatus::WorkRoot>("CardCacheStatus::WorkRoot");
    tableLaws<CardCacheStatus::GatherCounts>("CardCacheStatus::GatherCounts");
    tableLaws<GiStatus::VoxelSideCounts>("GiStatus::VoxelSideCounts");
    tableLaws<HeightFogStatus>("HeightFogStatus");
    tableLaws<CloudStatus>("CloudStatus");
    tableLaws<AtmosphereStatus>("AtmosphereStatus");
    tableLaws<RayQueryStatus>("RayQueryStatus");
    tableLaws<RayQueryStatus::ReflectEmitter>("RayQueryStatus::ReflectEmitter");
    tableLaws<RayQueryStatus::HitListView>("RayQueryStatus::HitListView");
    tableLaws<SunContactStatus>("SunContactStatus");
    tableLaws<AreaVisStatus>("AreaVisStatus");
    tableLaws<MobilityStatus>("MobilityStatus");
    tableLaws<RigStats>("RigStats");
    tableLaws<SceneChangeStats>("SceneChangeStats");
    tableLaws<GpuSceneStatus>("GpuSceneStatus");
    tableLaws<BonePose>("BonePose");
    tableLaws<FrameInputStatus>("FrameInputStatus");
    tableLaws<VrStatus>("VrStatus");
    tableLaws<VrPose>("VrPose");
    tableLaws<VrHandState>("VrHandState");
    tableLaws<VrProfileName>("VrProfileName");
    tableLaws<VrInfo>("VrInfo");
    tableLaws<VrBindingBlock>("VrBindingBlock");
    tableLaws<StreamingWork>("StreamingWork");
    tableLaws<AsyncShaderStats>("AsyncShaderStats");
    tableLaws<AsyncShaderStats::Burst>("AsyncShaderStats::Burst");
    tableLaws<MonitorStatus>("MonitorStatus");
    tableLaws<ShadowStatus>("ShadowStatus");
    tableLaws<ShadowMapInfo>("ShadowMapInfo");
    tableLaws<RenderStats>("RenderStats");
    tableLaws<MonitorRecord>("MonitorRecord");
    tableLaws<FrameStage>("FrameStage");
    tableLaws<FramePass>("FramePass");
    tableLaws<CacheWork>("CacheWork");
    tableLaws<MonitorEvent>("MonitorEvent");
    tableLaws<FrameSubmitStatus>("FrameSubmitStatus");
    tableLaws<ShaderCacheStats>("ShaderCacheStats");
    tableLaws<AtomDrawStatus>("AtomDrawStatus");
    tableLaws<AtomCasterMapStats>("AtomCasterMapStats");
    static_assert(sizeof(VrPose) == 32, "engine.published's doc states vrJoints' VrPose as 32 bytes: say its new layout");

    // 2. THE COMMANDS ON NO ENGINE: their arguments' laws, then the body (the engine absent, by name)
    Host &h = f.none;
    const auto reached = [](const Result &r) { return failedWith(r, "the engine did not start"); };
    const Result level = h.call("engine.monitor.set", {Value("loud")});
    const Result outside = f.outside.call("engine.monitor.set", {Value("review")});
    const Result outRecords = f.outside.call("engine.monitor.popRecords", {});
    const Result outEvents = f.outside.call("engine.monitor.popEvents", {});
    check(failedWith(outRecords, "engine.monitor.popRecords: engine-wide: this host serves an outside client") &&
              failedWith(outEvents, "engine.monitor.popEvents: engine-wide: this host serves an outside client"),
          "THE MONITOR's ONE QUEUE IS ENGINE-WIDE: an outside client's host cannot pop the owner's records or events (" +
              outRecords.message + ")");
    check(failedWith(level, "engine.monitor.set: level 'loud' is not one of \"off\"|\"review\"") &&
              failedWith(outside, "engine.monitor.set: engine-wide: this host serves an outside client") &&
              reached(h.call("engine.monitor.set", {Value("review")})),
          "engine.monitor.set: a level outside its names FAILS by name (" + level.message + "); ENGINE-WIDE: an outside "
          "client's host refuses it (" + outside.message + "); THE CONTROL: in its domain it reaches the body");
    bool all = true;
    std::string names;
    for (const char *n : {"engine.published", "engine.monitor.popRecords", "engine.monitor.popEvents",
                          "engine.frameSubmitStatus", "engine.shaderCacheStats"}) {
        all = all && reached(h.call(n, {}));
        names += std::string(" ") + n;
    }
    check(all, "with no engine each engine-wide read reaches its body and FAILS saying why:" + names);

    // 2b. A VIEW's ROW AND ITS EXPOSURE (no view on the headless engine: their EFFECT is a card case of the train): a
    //     stand-in view carries each call to its laws - select refuses an unknown member by name, a scene handle where
    //     a view is declared FAILS by kind; THE CONTROL: in their domain they reach the body
    {
        alignas(std::max_align_t) static unsigned char standIn[4096];
        const Value view(h.registry().hold(kView, std::shared_ptr<View>(reinterpret_cast<View *>(standIn), [](View *) {})));
        const Value scene(h.registry().hold(kScene, std::shared_ptr<Scene>(reinterpret_cast<Scene *>(standIn), [](Scene *) {})));
        const auto sel1 = [](const char *n) {
            return Value(Value::Object{{"select", Value(Value::Array{Value(n)})}});
        };
        const Result nope = h.call("engine.view.published", {view, sel1("nope")});
        const Result kind = h.call("engine.view.published", {scene, Value(Value::Object{})});
        const Result kindX = h.call("engine.view.exposureScale", {scene});
        check(failedWith(nope, "engine.view.published: options.select[0] 'nope' is not one of") &&
                  failedWith(kind, "is a scene handle, not a view handle") &&
                  failedWith(kindX, "engine.view.exposureScale: view:") &&
                  reached(h.call("engine.view.published", {view, sel1("routeGather")})) &&
                  reached(h.call("engine.view.exposureScale", {view})),
              "engine.view.published: an unknown member FAILS by name (" + nope.message + "), a scene handle where a view "
              "is declared FAILS (" + kind.message + "), and engine.view.exposureScale's too; THE CONTROL: both reach "
              "their body with a view");
        h.registry().release(view.asInt());
        h.registry().release(scene.asInt());
    }

    // 3. ON THE HEADLESS ENGINE: the reads answer their records, the engine's own published frame round-trips
    if (!f.engine) return;
    Host &e = f.headless;
    const Result sc = e.call("engine.scene.create", {Value("r-status")});
    const Result fr = e.call("engine.frames", {Value(2)});
    const Result pub = e.call("engine.published", {});
    PublishedEngineFrame back;
    std::string why;
    const bool read = pub.status == Status::Ok && Record<PublishedEngineFrame>::table().fromValue(pub.value, back, "", why);
    check(sc.status == Status::Ok && fr.status == Status::Ok && read && toValue(back) == pub.value &&
              back.frame == f.engine->published().frame && back.frame > 0,
          "engine.published answers the engine's published frame (frame " + std::to_string(back.frame) + ", the "
          "engine's " + std::to_string(f.engine->published().frame) + ") and it round-trips through its table " + why);
    // THE SELECTOR: only the members asked, in the order asked, each the whole record's value; an unknown one FAILS
    const auto sel = [](std::initializer_list<const char *> names) {
        Value::Array a;
        for (const char *n : names) a.push_back(Value(n));
        return Value(Value::Object{{"select", Value(std::move(a))}});
    };
    const Result two = e.call("engine.published", {sel({"serial", "frame"})});
    const Result unknown = e.call("engine.published", {sel({"frame", "nope"})});
    const bool exact = two.status == Status::Ok && two.value.asObject().size() == 2 &&
                       two.value.asObject()[0].first == "serial" && two.value.asObject()[1].first == "frame" &&
                       two.value["frame"] == pub.value["frame"] && two.value["serial"] == pub.value["serial"];
    check(exact && failedWith(unknown, "nope"),
          "engine.published({select: [serial, frame]}) answers exactly those two members in that order, equal to the "
          "whole record's; a member it lacks FAILS by name (" + unknown.message + ")");
    const Result sub = e.call("engine.frameSubmitStatus", {});
    const Result cache = e.call("engine.shaderCacheStats", {});
    check(sub.status == Status::Ok && sub.value["submitted"] == Value(f.engine->frameSubmitStatus().submitted) &&
              sub.value["submitted"].asInt() > 0 && cache.status == Status::Ok && cache.value.kind() == Kind::Object,
          "engine.frameSubmitStatus answers the engine's (submitted " +
              (sub.status == Status::Ok ? std::to_string(sub.value["submitted"].asInt()) : sub.message) +
              " > 0: the frames stepped the host's scene); engine.shaderCacheStats answers its record");
    const Result on = e.call("engine.monitor.set", {Value("review")});
    e.call("engine.frames", {Value(3)});
    const Result off = e.call("engine.monitor.set", {Value("off")});
    const Result recs = e.call("engine.monitor.popRecords", {});
    const Result evs = e.call("engine.monitor.popEvents", {});
    const Result again = e.call("engine.monitor.popRecords", {});
    check(on.status == Status::Ok && off.status == Status::Ok && recs.status == Status::Ok && recs.value.kind() == Kind::Array &&
              !recs.value.asArray().empty() && evs.status == Status::Ok && evs.value.kind() == Kind::Array &&
              !evs.value.asArray().empty() && again.status == Status::Ok && again.value.asArray().empty(),
          "the monitor on the headless engine: set review, three frames, set off, popRecords answers (above zero: THE "
          "CONTROL) " +
              std::to_string(recs.status == Status::Ok ? recs.value.asArray().size() : 0) + " record(s) and popEvents " +
              std::to_string(evs.status == Status::Ok ? evs.value.asArray().size() : 0) +
              " event(s); the queue is EMPTY after its pop (the second pop answers none)");
    // THE SCENE's PUBLISHED FRAME round-trips as the engine wrote it; and the GI engine.gi.set gives is READ BACK through
    // the script's own command: engine.gi.set is DIRECT (Scene::setGlobalIllumination: its refusal synchronous), so the
    // published GiStatus carries it and giApplied - the RECORD ROUTE's field, written only by the FrameInput drain's
    // part.gi branch (OgreEngine.cpp:3462-3471: the app's mirror) - stays null (the lead's ruling, 2026-11-23)
    {
        const Result gi = e.call("engine.gi.set",
                                 {sc.value, Value(Value::Object{{"mode", Value("vct")}, {"quality", Value("low")},
                                                                {"updateBudget", Value(3)},
                                                                {"cascades", Value(Value::Array{Value(Value::Object{
                                                                    {"halfSize", Value(8.0)}, {"resolution", Value(32)}})})}})});
        e.call("engine.frames", {Value(1)});
        const Result sp = e.call("engine.scene.published", {sc.value, Value(Value::Object{})});
        PublishedFrame pf;
        const bool spRead = sp.status == Status::Ok && Record<PublishedFrame>::table().fromValue(sp.value, pf, "", why);
        check(spRead && toValue(pf) == sp.value && pf.frame > 0,
              "engine.scene.published answers the scene's published frame (frame " + std::to_string(pf.frame) +
                  ") and it round-trips through its table " + sp.message + why);
        const Result ga = e.call("engine.scene.published", {sc.value, sel({"gi", "giApplied"})});
        const bool read = ga.status == Status::Ok && ga.value.kind() == Kind::Object;
        const Value budget = read ? ga.value["gi"]["updateBudget"] : Value();
        const Value applied = read ? ga.value["giApplied"] : Value();
        const long long got = budget.kind() == Kind::Int ? budget.asInt() : -1;
        check(gi.value == Value(true) && got == 3 && read && applied.kind() == Kind::Null,
              "engine.gi.set (vct, low, updateBudget 3), then engine.scene.published's gi.updateBudget reads " +
                  std::to_string(got) + " (3: the direct setter reached the engine - THE CONTROL, above zero) and giApplied "
                  "is NULL (the record route's field: a script's GI never shows there) (" + gi.message + ga.message + ")");
    }
    const Result atom = e.call("engine.scene.atomDrawStatus", {sc.value});
    AtomDrawStatus atomBack;
    check(atom.status == Status::Ok && Record<AtomDrawStatus>::table().fromValue(atom.value, atomBack, "", why) &&
              toValue(atomBack) == atom.value,
          "engine.scene.atomDrawStatus answers the headless scene's record and it round-trips (" + atom.message + why + ")");
    if (sc.status == Status::Ok) e.call("engine.scene.destroy", {sc.value});
}

}  // namespace scriptcommands
