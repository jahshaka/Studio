// script.commands - THE ENGINE FAMILY's CASES (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp runs them, in its
// fixed order): the engine-wide commands' declared domains refused by name on the null engine, every engine-wide one
// refused for an OUTSIDE client (Commands::engineWide) and reaching the engine otherwise, the answers' tables in order
// and round-tripping, and what the headless engine answers (each case prints what it read).
#include "script_commands.h"
#include "script/records_engine.h"

#include <cstdio>

namespace scriptcommands {

void casesEngine(Fixture &f)
{
    // 1. THE ANSWERS' TABLES
    {
        std::string w1, w2;
        TextureMemoryEntry t;
        t.name = "a"; t.resource = "b.png"; t.width = 2; t.height = 3; t.depth = 4; t.slices = 5; t.mipmaps = 6; t.msaa = 4;
        t.format = "PFG_RGBA8"; t.bytes = 1ull << 40; t.renderTarget = true; t.uav = true; t.manual = true; t.pooled = true;
        t.residency = "Resident";
        ValidationStatus v;
        v.requested = true; v.active = true; v.layers = {"libVkLayer_khronos_validation.so"}; v.drawEntry = "x";
        const auto trip = [](const auto &in) {
            std::decay_t<decltype(in)> back;
            std::string why;
            return Record<std::decay_t<decltype(in)>>::table().fromValue(toValue(in), back, "", why) && toValue(back) == toValue(in);
        };
        check(Record<TextureMemoryEntry>::table().inOrder(w1) && Record<ValidationStatus>::table().inOrder(w2) &&
                  trip(TextureMemoryEntry()) && trip(t) && trip(ValidationStatus()) && trip(v),
              "TextureMemoryEntry and ValidationStatus: their tables in declaration order, their defaults and filled values "
              "round-trip" + w1 + w2);
    }

    // 2. THE DECLARED DOMAINS (no engine) and THE TEST DOOR shut
    {
        const struct { Result r; const char *text; } refused[] = {
            {f.none.call("engine.shaders.flushCache", {Value(-1)}), "engine.shaders.flushCache: budgetMs -1 is outside 0..60000 ms"},
            {f.none.call("engine.shaders.flushCache", {Value(70000)}), "budgetMs 70000 is outside 0..60000 ms"},
            {f.none.call("engine.display.setPeriod", {Value(-0.5)}), "seconds -0.5 is outside >= 0 s"},
            {f.none.call("engine.publishRenderStats", {Value(-1)}), "frames -1 is outside 0..1000000 frames"},
            {f.none.call("engine.publishShadowStatus", {Value(2.5)}), "frames must be an integer"},
            {f.none.call("engine.shaders.placeholdersOnly", {Value(true)}),
             "engine.shaders.placeholdersOnly: a test door: this host was not started with --test-doors"},
            {f.none.call("engine.rayQueryAvailable", {Value(1)}), "engine.rayQueryAvailable"},
        };
        std::string missed;
        for (const auto &x : refused)
            if (!failedWith(x.r, x.text)) missed += "\n      " + std::string(x.text) + " <- " + x.r.message;
        check(missed.empty(), "the engine-wide commands refuse their declared domains by name, the placeholders' door shut "
                              "without --test-doors" + missed);
    }

    // 3. AN ENGINE-WIDE COMMAND IS REFUSED FOR AN OUTSIDE CLIENT, by name, before the engine is asked; THE CONTROL: the
    //    same call in a host that serves no outside client reaches the engine (absent here: "did not start")
    {
        const struct { const char *name; Value::Array args; } wide[] = {
            {"engine.shaders.saveCache", {}}, {"engine.shaders.flushCache", {Value(0)}}, {"engine.shaders.prepareRays", {}},
            {"engine.display.setPeriod", {Value(1.0 / 60.0)}}, {"engine.takeLastError", {}},
            {"engine.publishRenderStats", {Value(2)}}, {"engine.publishShadowStatus", {Value(2)}}};
        std::string missed;
        int refusedHere = 0, reached = 0;
        for (const auto &w : wide) {
            const Result out = f.outside.call(w.name, w.args), in = f.none.call(w.name, w.args);
            if (failedWith(out, std::string(w.name) + ": engine-wide: this host serves an outside client")) ++refusedHere;
            else missed += "\n      " + std::string(w.name) + " <- " + out.message;
            if (in.message.find("the engine did not start") != std::string::npos) ++reached;
        }
        check(missed.empty() && reached == int(sizeof wide / sizeof wide[0]),
              "every engine-wide setter (" + std::to_string(refusedHere) + ") is refused for an outside client; THE CONTROL: "
              "each reaches the engine in a host that serves none (" + std::to_string(reached) + ")" + missed);
    }

    // 4. ON THE HEADLESS ENGINE
    if (!f.engine) {
        check(false, "the engine family's headless cases need the headless engine");
        return;
    }
    Host &h = f.headless;
    const Result wait = h.call("engine.shaders.wait", {});
    const Result progress = h.call("engine.shaders.progress", {});
    const Result save = h.call("engine.shaders.saveCache", {});
    const Result flush = h.call("engine.shaders.flushCache", {Value(0)});
    const Result rays = h.call("engine.shaders.prepareRays", {});
    const Result period = h.call("engine.display.setPeriod", {Value(1.0 / 90.0)});
    const Result stats = h.call("engine.publishRenderStats", {Value(2)});
    const Result shadow = h.call("engine.publishShadowStatus", {Value(2)});
    const Result rq = h.call("engine.rayQueryAvailable", {});
    const Result mem = h.call("engine.textureMemory", {});
    const Result val = h.call("engine.validation", {});
    std::printf("Q: the headless engine's engine-wide commands: wait %s, progress %s, saveCache %s, flushCache %s, rayQuery %s, "
                "textureMemory %s (%zu), validation %s\n", wait.status == Status::Ok ? "ok" : wait.message.c_str(),
                progress.status == Status::Ok ? "ok" : progress.message.c_str(), save.value == Value(true) ? "true" : "false",
                flush.value == Value(true) ? "true" : "false", rq.value == Value(true) ? "true" : "false",
                mem.status == Status::Ok ? "ok" : mem.message.c_str(), mem.value.kind() == Kind::Array ? mem.value.asArray().size() : 0,
                val.status == Status::Ok ? "ok" : val.message.c_str());
    check(wait.status == Status::Ok && progress.status == Status::Ok && progress.value["compiled"].kind() == Kind::Int &&
              progress.value["expected"].kind() == Kind::Int && save.status == Status::Ok && flush.value == Value(true) &&
              rays.status == Status::Ok && period.status == Status::Ok && stats.status == Status::Ok &&
              shadow.status == Status::Ok && rq.value == Value(false) && val.value["requested"].kind() == Kind::Bool &&
              val.value["active"] == Value(false),
          "the engine-wide commands act on the headless engine: the shaders' wait, progress {compiled, fromCache, expected}, "
          "save, an idle writer's flush (true), the ray pipelines (a no-op), the display period, both subscriptions; the "
          "NULL render system traces no rays and runs no validation layer (both false)");
    // THE SINK IS TAKEN AND CLEARED: an engine refusal fills it, the take answers its words, a second take answers ""
    Scene *sc = f.engine->createScene("q-engine-sink");
    const bool refusedNow = !sc->destroyTexture(TextureId(0xFFFFFF));
    const Result take1 = h.call("engine.takeLastError", {}), take2 = h.call("engine.takeLastError", {});
    f.engine->destroyScene(sc);
    check(refusedNow && take1.value.kind() == Kind::String && !take1.value.asString().empty() && take2.value == Value(""),
          "engine.takeLastError answers the engine's last refusal (" + (take1.value.kind() == Kind::String ? take1.value.asString() : take1.message) +
              ") and clears it: the second take answers \"\" (THE CONTROL: the first is not empty)");
}

}  // namespace scriptcommands
