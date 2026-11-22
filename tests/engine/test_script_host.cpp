// script.host — THE ENGINE'S SCRIPT HOST, ITS LAWS ON THE CPU (SCRIPT-IN-ENGINE-1 stage 1; jahshaka/engine/Script.h).
// No device: a registry of this file's own commands, and ONE HEADLESS (NULL) ENGINE (no card, no display) whose real scenes
// and nodes the engine commands' argument laws are proved on - each failure before the engine is asked, each control
// reaching a real engine.
// THE LAWS: a value of every kind is itself after a copy and compares by content (an Int equals a Number of the same
// number); every command carries a name and a doc (validate is empty); THE STRICT CALL — an unregistered name, a surplus
// argument, an undeclared object member, a missing argument with no default and a non-integral number for an integer
// each FAIL by name; null and absent take the declared default; an integral number is an integer; a fail is the
// caller's mistake (Failed, null), a refuse the engine's state (Refused, the declared return's falsy value, the message
// kept as lastError). Its own commands are tagged as the engine's are: one that holds, releases or stores state (test.make
// of a handle, test.free, test.keep, test.ping) CHANGES it; the rest READ.
#include "jahshaka/engine/Script.h"
#include "enginetesthelpers.h"         // the headless (NULL) engine's config: no Qt, no card, no display
#include "script/RouteDeclaration.h"   // the route declaration's judgement (engine-private, pure)
#include "script/records.h"            // ONE FIELD TABLE PER RECORD (engine-private): the machinery, on this file's records

#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include "jahshaka/engine/FrameInput.h"
#include "jahshaka/engine/Ids.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Math.h"
#include "jahshaka/engine/Published.h"

using namespace jahshaka::engine;
using namespace jahshaka::engine::script;

// THE FILE's OWN RECORDS (2b b1): every generic a field table carries - bool, an enum, integers, float, double, a
// string, Quat, Colour, a nested record, a vector of records, a FLAT vector of Vec3, a float[3], an optional and a
// shared_ptr<const T>
namespace tr {
enum class Mode { Off, Fast, Slow };
enum class Partial { Named, AlsoNamed, Unnamed };   // (12f: its table names two of three)
struct Inner {
    int level = 2;
    Vec3 at{1, 2, 3};
};
struct Outer {
    bool on = true;
    Mode mode = Mode::Fast;
    unsigned count = 7;
    float gain = 0.5f;
    double exact = 0.125;
    std::string label = "x";
    Quat turn;
    Colour tint{0.1f, 0.2f, 0.3f, 1.0f};
    Inner inner;
    std::vector<Inner> list;
    std::vector<Vec3> points;
    std::vector<Quat> turns;
    std::vector<Colour> tints;
    float sh[3] = {1, 2, 3};
    std::optional<int> maybe;
    std::shared_ptr<const Inner> shared;
};
}  // namespace tr
namespace jahshaka { namespace engine { namespace script {
JAH_ENUM_NAMES(tr::Mode, {{tr::Mode::Off, "off"}, {tr::Mode::Fast, "fast"}, {tr::Mode::Slow, "slow"}});
JAH_ENUM_NAMES(tr::Partial, {{tr::Partial::Named, "named"}, {tr::Partial::AlsoNamed, "alsoNamed"}});
JAH_RECORD_COUNT(tr::Inner, 2);
JAH_RECORD_COUNT(tr::Outer, 16);
const Table<tr::Inner> &Record<tr::Inner>::table()
{
    static const Table<tr::Inner> t({field("level", &tr::Inner::level).range(0, 9), field("at", &tr::Inner::at)});
    return t;
}
const Table<tr::Outer> &Record<tr::Outer>::table()
{
    static const Table<tr::Outer> t({field("on", &tr::Outer::on), field("mode", &tr::Outer::mode),
                                     field("count", &tr::Outer::count), field("gain", &tr::Outer::gain),
                                     field("exact", &tr::Outer::exact), field("label", &tr::Outer::label),
                                     field("turn", &tr::Outer::turn), field("tint", &tr::Outer::tint),
                                     field("inner", &tr::Outer::inner), field("list", &tr::Outer::list).upTo(4),
                                     field("points", &tr::Outer::points).upTo(12), field("turns", &tr::Outer::turns).upTo(8),
                                     field("tints", &tr::Outer::tints).upTo(8), field("sh", &tr::Outer::sh),
                                     field("maybe", &tr::Outer::maybe), field("shared", &tr::Outer::shared)});
    return t;
}
}}}  // namespace jahshaka::engine::script

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    if (!ok) ++gFail;
}
static bool failedWith(const Result &r, const std::string &part)
{
    return r.status == Status::Failed && r.value.isNull() && r.message.find(part) != std::string::npos;
}

int main()
{
    // 1. THE VALUE: every kind, copied, compared by content
    const Value::Object obj{{"b", Value(2)}, {"a", Value("x")}};
    const Value all[] = {Value(), Value(true), Value(std::int64_t(1) << 60), Value(2.5), Value("utf-8 \xc3\xa9"),
                         Value(Value::Array{Value(1), Value("two"), Value()}), Value(obj),
                         Value::bytes({0, 1, 255})};
    const Kind kinds[] = {Kind::Null, Kind::Bool, Kind::Int, Kind::Number, Kind::String, Kind::Array, Kind::Object,
                          Kind::Bytes};
    bool same = true;
    for (int i = 0; i < 8; ++i) {
        const Value copy = all[i];
        same = same && copy.kind() == kinds[i] && copy == all[i];
        for (int j = 0; j < 8; ++j) same = same && (i == j || all[i] != all[j]);
    }
    check(same, "a value of each of the eight kinds is itself after a copy, and differs from the other seven");
    check(Value(3) == Value(3.0) && Value(3) != Value(3.5), "an Int equals a Number of the same number, not another");
    check(Value(obj).asObject()[0].first == "b" && Value(obj)["a"] == Value("x") && Value(obj)["c"].isNull(),
          "an object keeps its members' order and answers a member by name (an absent one null)");

    // 2. A REGISTRY of this file's commands
    Registry reg;
    const bool added =
        reg.add({"test.add", {Access::Reads, {target("a", Kind::Int), object("more", {arg("b", Kind::Int, Value(10))}, Value(Value::Object{}))},
                             Kind::Int}, "a + more.b"},
                [](Call &c) { return Value(c[0].asInt() + c[1]["b"].asInt()); }) &&
        reg.add({"test.opts", {Access::Reads, {object("opts", {arg("n", Kind::Number, Value(1.0)), numbers("v", 3)})}, Kind::Number},
                 "opts.n + the sum of opts.v"},
                [](Call &c) {
                    double s = c[0]["n"].asNumber();
                    for (const Value &x : c[0]["v"].asArray()) s += x.asNumber();
                    return Value(s);
                }) &&
        reg.add({"test.mistake", {Access::Reads, {}, Kind::Bool}, "fails: the caller's mistake"},
                [](Call &c) { return c.fail("you asked for a mistake"); }) &&
        reg.add({"test.notNow", {Access::Reads, {}, Kind::Bool}, "refuses: the engine's state"},
                [](Call &c) { return c.refuse("not now"); });
    check(added && reg.validate().empty(),
          "four commands registered; validate is empty: every command has a name, a signature and a doc, and its shape");
    Registry twice;
    const bool first = twice.add({"test.one", {Access::Reads, {}, Kind::Null}, "once"}, [](Call &) { return Value(); });
    const bool second = twice.add({"test.one", {Access::Reads, {}, Kind::Null}, "again"}, [](Call &) { return Value(); });
    check(first && !second && twice.validate().size() == 1 &&
              twice.validate()[0].find("test.one: registered twice") == 0 && twice.commands().size() == 1,
          "a second command of one name is refused AND kept as a fault (" +
              (twice.validate().empty() ? std::string("none") : twice.validate()[0]) + ")");
    Registry undocumented;
    undocumented.add({"test.bare", {Access::Reads, {arg("", Kind::Int)}, Kind::Null}, ""}, [](Call &) { return Value(); });
    check(undocumented.validate().size() == 2, "validate names a command with no doc and a parameter with no name");
    Registry misshapen;
    misshapen.add({"test.pair", {Access::Reads, {arg("a", Kind::Int), arg("b", Kind::Int)}, Kind::Null}, "two positional numbers"},
                  [](Call &) { return Value(); });
    misshapen.add({"test.late", {Access::Reads, {arg("opts", Kind::Bool), target("scene", Kind::Int)}, Kind::Null}, "a target last"},
                  [](Call &) { return Value(); });
    const std::vector<std::string> faults = misshapen.validate();
    check(faults.size() == 2 && faults[0].find("test.pair: 2 positional parameters are not targets") == 0 &&
              faults[1].find("test.late: the target 'scene' follows a named parameter") == 0,
          "THE SHAPE RULE: validate names two non-target positional parameters and a target after a named one");
    Registry untagged;
    const bool tagless = untagged.add({"test.untagged", {Access::Unset, {}, Kind::Null}, "declares no access"},
                                      [](Call &) { return Value(); });
    check(!tagless && untagged.validate().size() == 1 && untagged.validate()[0] == "test.untagged: no access tag" &&
              untagged.commands().empty(),
          "THE ACCESS TAG: a command that declares no access is refused and kept as a fault (" +
              (untagged.validate().empty() ? std::string("none") : untagged.validate()[0]) + ")");

    Registry unbounded;
    const bool open = unbounded.add({"test.open", {Access::Reads, {object("o", {arg("v", Kind::Array, Value(Value::Array{}))})},
                                                   Kind::Null}, "an array with no bound"},
                                    [](Call &) { return Value(); });
    check(!open && unbounded.validate().size() == 1 &&
              unbounded.validate()[0] == "test.open: the array 'v' declares neither a count nor a maximum" &&
              unbounded.commands().empty(),
          "AN ARRAY DECLARES ITS BOUND: one with neither a count nor a maximum is refused and kept as a fault (" +
              (unbounded.validate().empty() ? std::string("none") : unbounded.validate()[0]) + ")");

    // 3. THE CALL BY NAME, AND ITS LAWS
    const auto more = [](int b) { return Value(Value::Object{{"b", Value(b)}}); };
    Result r = reg.call("test.add", {Value(2), more(3)});
    check(r.status == Status::Ok && r.value == Value(5), "test.add(2, {b: 3}) = 5 by name");
    check(failedWith(reg.call("test.nothing", {}), "test.nothing is not a command"),
          "an unregistered name FAILS, naming it");
    check(reg.call("test.add", {Value(2)}).value == Value(12) && reg.call("test.add", {Value(2), Value()}).value == Value(12),
          "an absent or null argument takes its declared default");
    check(failedWith(reg.call("test.add", {}), "a is required") && failedWith(reg.call("test.add", {Value()}), "a is required"),
          "an absent or null argument with no default FAILS, naming it");
    check(reg.call("test.add", {Value(3.0), more(4)}).value == Value(7), "an integral number (3.0) is an integer");
    check(failedWith(reg.call("test.add", {Value(3.7)}), "a must be an integer"),
          "3.7 for an integer FAILS (never truncated to 3)");
    check(failedWith(reg.call("test.add", {Value(1), more(2), Value(3)}), "3 arguments given, it takes 2"),
          "a surplus argument FAILS");
    check(failedWith(reg.call("test.add", {Value("2")}), "a must be int"), "a string for an integer FAILS");
    const Value v3(Value::Array{Value(1), Value(2), Value(3)});
    check(reg.call("test.opts", {Value(Value::Object{{"v", v3}})}).value == Value(7.0),
          "an object's absent member takes its default (n = 1: 1 + 1 + 2 + 3)");
    check(failedWith(reg.call("test.opts", {Value(Value::Object{{"v", v3}, {"m", Value(1)}})}), "opts has no member 'm'"),
          "an object's undeclared member FAILS, naming it (a misspelt option is never dropped)");
    check(failedWith(reg.call("test.opts", {Value(Value::Object{{"n", Value(1)}})}), "opts.v is required"),
          "an object's required member absent FAILS, naming it");
    check(failedWith(reg.call("test.opts", {Value(Value::Object{{"v", Value(Value::Array{Value(1)})}})}),
                     "opts.v must hold 3 elements"),
          "a vector of the wrong length FAILS, naming it");
    r = reg.call("test.mistake", {});
    check(failedWith(r, "test.mistake: you asked for a mistake") && reg.lastError().empty(),
          "a fail is Failed with its message, its value null, and is no refusal (lastError untouched)");
    r = reg.call("test.notNow", {});
    check(r.status == Status::Refused && r.value == Value(false) && r.message == "test.notNow: not now" &&
              reg.lastError() == "test.notNow: not now",
          "a refuse answers the declared return's falsy value (false) and keeps its message as lastError");
    check(signatureText(*reg.find("test.add")) == "test.add(a: target int, more: object {b: int = default} = default) -> int [reads]",
          "the signature reads as text: " + signatureText(*reg.find("test.add")));

    // 4. THE HOST AND ITS SCRIPT LANGUAGE (JavaScript): the same registry's commands as functions of their roots
    Host host;
    std::vector<std::string> lines;
    host.setConsole([&lines](const std::string &l) { lines.push_back(l); });
    Registry &hr = host.registry();
    hr.add({"test.add", {Access::Reads, {target("a", Kind::Int), object("more", {arg("b", Kind::Int, Value(10))}, Value(Value::Object{}))},
                        Kind::Int}, "a + more.b"},
           [](Call &c) { return Value(c[0].asInt() + c[1]["b"].asInt()); });
    hr.add({"test.opts", {Access::Reads, {object("opts", {arg("n", Kind::Number, Value(1.0)), numbers("v", 3)})}, Kind::Number},
            "opts.n + the sum of opts.v"},
           [](Call &c) {
               double s = c[0]["n"].asNumber();
               for (const Value &x : c[0]["v"].asArray()) s += x.asNumber();
               return Value(s);
           });
    hr.add({"test.mistake", {Access::Reads, {}, Kind::Bool}, "fails: the caller's mistake"},
           [](Call &c) { return c.fail("you asked for a mistake"); });
    hr.add({"test.notNow", {Access::Reads, {}, Kind::Bool}, "refuses: the engine's state"},
           [](Call &c) { return c.refuse("not now"); });
    // NEUTRALITY: `test.make` answers one value of every kind; `test.keep` stores what reaches it (declared field by field)
    const Value made(Value::Object{{"nul", Value()}, {"yes", Value(true)}, {"i", Value(-7)},
                                   {"big", Value(std::int64_t(1) << 60)}, {"x", Value(0.25)},
                                   {"s", Value("utf-8 \xc3\xa9\xe2\x82\xac")},
                                   {"v", Value(Value::Array{Value(1), Value(2.5), Value(-3)})},
                                   {"o", Value(Value::Object{{"k", Value("in")}})}, {"raw", Value::bytes({0, 7, 255})}});
    const Param keepParam = object("v", {arg("nul", Kind::Null, Value()), arg("yes", Kind::Bool), arg("i", Kind::Int),
                                         arg("big", Kind::Int), arg("x", Kind::Number), arg("s", Kind::String),
                                         numbers("v", 3), object("o", {arg("k", Kind::String)}),
                                         arg("raw", Kind::Bytes)});
    Value kept;
    hr.add({"test.make", {Access::Reads, {}, Kind::Object}, "one value of every kind"}, [&made](Call &) { return made; });
    hr.add({"test.keep", {Access::Changes, {keepParam}, Kind::Null}, "stores its argument"},
           [&kept](Call &c) { kept = c[0]; return Value(); });
    hr.call("test.keep", {hr.call("test.make", {}).value});
    const Value byCpp = kept;
    kept = Value();
    Result run = host.run("test.keep(test.make());\n", "neutral.js");
    check(run.status == Status::Ok && byCpp == made && kept == byCpp,
          "NEUTRALITY: test.keep(test.make()) through JavaScript stores the same value as through the C++ registry "
          "(every kind, a 2^60 integer as a BigInt and back, bytes as a Uint8Array and back)");

    lines.clear();
    run = host.run("console.log('two', 1 + 1, [3, 4]);\nconsole.log(test.add(2, {b: 3}), test.add(2), test.opts({v: [1, 2, 3]}));\n",
                   "console.js");
    check(run.status == Status::Ok && lines.size() == 2 && lines[0] == "two 2 3,4" && lines[1] == "5 12 7",
          "console lines are captured, and commands answer by name in JS (" + (lines.empty() ? "" : lines.back()) + ")");

    lines.clear();
    run = host.run("try {\n  test.mistake();\n} catch (e) {\n  console.log(e.message);\n  console.log(e.stack);\n}\n",
                   "catch.js");
    check(run.status == Status::Ok && lines.size() == 2 && lines[0] == "test.mistake: you asked for a mistake" &&
              lines[1].find("catch.js:2") != std::string::npos,
          "a fail is a catchable JS error carrying its message and the script's line (" +
              (lines.size() == 2 && lines[1].find("catch.js") != std::string::npos
                   ? lines[1].substr(lines[1].find("catch.js"), 10) : std::string("?")) + ")");
    run = host.run("\ntest.add(3.7);\n", "uncaught.js");
    check(run.status == Status::Failed && run.message.find("test.add: a must be an integer") != std::string::npos &&
              run.message.find("uncaught.js:2") != std::string::npos,
          "an uncaught fail ends the run Failed with its message and line (3.7 for an integer, through JS)");
    const char *strict[][2] = {{"test.nothing();", "nothing"},
                               {"test.add(1, {b: 2}, 3);", "3 arguments given"},
                               {"test.opts({v: [1, 2, 3], m: 1});", "opts has no member 'm'"},
                               {"test.add();", "a is required"},
                               {"test.add(() => 1);", "a function cannot be carried"}};
    for (const auto &s : strict) {
        run = host.run(s[0], "strict.js");
        check(run.status == Status::Failed && run.message.find(s[1]) != std::string::npos,
              std::string("through JS, `") + s[0] + "` FAILS naming it: " + run.message.substr(0, run.message.find('\n')));
    }

    lines.clear();
    run = host.run("console.log(typeof test.add, String(test), 'add' in test);\n"
                   "try { const f = test.addd; } catch (e) { console.log(e.message); }\n", "read.js");
    check(run.status == Status::Ok && lines.size() == 2 && lines[0] == "function [object Object] true" &&
              lines[1] == "test.addd is not a command",
          "a namespace's commands are its own properties, it prints as an object, and an unknown name FAILS AT ITS READ "
          "(no call needed): " + (lines.size() == 2 ? lines[0] + " / " + lines[1] : std::string("?")));

    lines.clear();
    run = host.run("console.log(test.notNow(), host.lastError());\n", "refuse.js");
    check(run.status == Status::Ok && lines.size() == 1 && lines[0] == "false test.notNow: not now",
          "a refuse answers falsy in JS and host.lastError() holds its message (" + (lines.empty() ? "" : lines[0]) + ")");

    lines.clear();
    run = host.run("host.check({ok: true, what: 'held'});\nhost.check({ok: 1 > 2, what: 'broken'});\n", "check.js");
    check(run.status == Status::Ok && host.checksFailed() == 1 && host.checksRun() == 2 && lines.size() == 2 &&
              lines[0] == "ok:   held" && lines[1] == "FAIL: broken",
          "host.check prints each law and counts the checks that ran (2) and the failures (1)");

    // 4b. THE VERDICT (jahengine's): a script that checked nothing FAILS by name; the async work a script queued runs to
    //     its end; a rejected promise no handler took FAILS the run by name
    {
        Host quiet;
        std::vector<std::string> said;
        quiet.setConsole([&said](const std::string &l) { said.push_back(l); });
        const Result r0 = quiet.run("const x = 1 + 1;\n", "nothing.js");
        const int faults = quiet.verdict(r0);
        check(r0.status == Status::Ok && faults == 1 && !said.empty() &&
                  said.back().find("FAIL: the script checked nothing") == 0,
              "a script with no host.check FAILS its verdict by name (" + (said.empty() ? "" : said.back()) + ")");
    }
    {
        Host later;
        std::vector<std::string> said;
        later.setConsole([&said](const std::string &l) { said.push_back(l); });
        const Result r1 = later.run("async function t() {\n  await 0;\n  host.check({ok: true, what: 'after the await'});\n}\nt();\n",
                                    "await.js");
        check(r1.status == Status::Ok && later.checksRun() == 1 && later.verdict(r1) == 0,
              "a check after an `await` RUNS (the queued work is drained after the script): " +
                  std::to_string(later.checksRun()) + " check(s) ran");
    }
    {
        Host rejected;
        rejected.setConsole([](const std::string &) {});
        const Result r2 = rejected.run("host.check({ok: true, what: 'before'});\n"
                                       "Promise.reject(new Error('nobody handles me'));\n", "reject.js");
        check(r2.status == Status::Failed && r2.message.find("an unhandled promise rejection") != std::string::npos &&
                  r2.message.find("nobody handles me") != std::string::npos && rejected.verdict(r2) == 1,
              "a rejected promise nobody handles FAILS the run by name: " + r2.message.substr(0, r2.message.find('\n')));
        const Result r3 = rejected.run("Promise.reject(new Error('handled')).catch(() => host.check({ok: true, what: 'caught'}));\n",
                                       "handled.js");
        check(r3.status == Status::Ok && rejected.checksRun() == 2,
              "a rejection a handler takes is no fault, and the handler's check runs");
        const Result r4 = rejected.run("async function t() {\n  await 0;\n  throw new Error('late');\n}\nt();\n", "late.js");
        check(r4.status == Status::Failed && r4.message.find("late") != std::string::npos &&
                  r4.message.find("late.js:3") != std::string::npos,
              "an error thrown after an `await` FAILS the run with its message and line: " +
                  r4.message.substr(0, r4.message.find('\n')));
    }

    // 5. THE ENGINE'S COMMANDS WITH NO ENGINE (a process whose engine did not start still has a host that says why), and
    //    the picture's native reductions against their closed forms on a synthetic 4 x 2 picture
    Host engHost;
    registerEngineCommands(engHost, nullptr, "no device here", false);
    Registry &eng = engHost.registry();
    std::size_t engineCommands = 0;
    for (const CommandInfo *c : eng.commands()) engineCommands += c->name.rfind("engine.", 0) == 0;
    check(engineCommands == 29 && eng.validate().empty(),
          "the 29 engine commands register, each documented and shaped (targets positional, the rest named), with no engine");
    {
        // THE ACCESS of the 26 (the spec's table, 2a a1, + setTransforms): these read; every other changes the engine
        const std::vector<std::string> reads = {"host.check", "host.lastError", "engine.view.routes",
                                                "engine.view.declareRoutes", "engine.picture.bytes", "engine.picture.hash",
                                                "engine.picture.columnStep", "engine.picture.mean"};
        std::size_t checked = 0;
        std::string wrong;
        for (const CommandInfo *c : eng.commands()) {
            bool read = false;
            for (const std::string &n : reads) read = read || n == c->name;
            ++checked;
            if (c->signature.access != (read ? Access::Reads : Access::Changes)) wrong += " " + signatureText(*c);
        }
        check(checked == 31 && wrong.empty(),
              "every one of the " + std::to_string(checked) + " commands declares the access of the spec's table: 8 " +
                  "reads, the rest changes" + (wrong.empty() ? std::string() : " - wrong:" + wrong));
        check(signatureText(*eng.find("engine.picture.hash")) == "engine.picture.hash(picture: target picture handle) -> string [reads]",
              "the signature prints its access last: " + signatureText(*eng.find("engine.picture.hash")));
    }
    // ONE HEADLESS (NULL) ENGINE and a host over it: real scenes and nodes (the engine.exit_only_teardown pattern: the
    // harness's config, the exit-only teardown; the hosts hold no engine object - their scene handles never own one)
    std::string bootError;
    const std::unique_ptr<Engine> headless =
        Engine::create(enginetest::testEngineConfig("test-script-host-ogre.log", enginetest::EngineOverrides().headless()),
                       bootError);
    check(headless != nullptr, "a headless (NULL) engine boots for the commands' laws (" + bootError + ")");
    Host liveHost;
    registerEngineCommands(liveHost, headless.get(), bootError, false);
    Registry &live = liveHost.registry();
    const Value realScene = live.call("engine.scene.create", {Value("script-host")}).value;
    const Value realNode = live.call("engine.node.create", {realScene}).value;
    check(live.object<Scene>(realScene.asInt()) && realNode.kind() == Kind::Int,
          "the live host makes a real scene and a real node through its commands");
    check(failedWith(live.call("engine.node.setTransform",
                               {realScene, realNode, Value(Value::Object{{"position", Value(Value::Array{Value(0), Value(1)})}})}),
                     "engine.node.setTransform: transform.position must hold 3 elements") &&
              live.call("engine.node.setTransform", {realScene, realNode, Value(Value::Object{})}).status == Status::Ok,
          "a 2-element position FAILS by name (the host checks a vector's length, before the engine is asked); THE "
          "CONTROL: the whole call reaches the real engine");
    // A HELD KIND IS BOUND TO ONE C++ TYPE AT REGISTRATION (2b; MCP-ON-HOST-1 stage 1's finding, the lead's ruling):
    // in a host with the engine's commands no hold claims an engine kind for another type, in any order; a stand-in is
    // a kind of the test's own, refused where a scene is declared before anything is read; a kind bound before the
    // engine's registration is a FAULT; and a handle is minted only by the door that owns its kind
    {
        std::string why, mint;
        const std::int64_t claimed = eng.hold("scene", std::make_shared<int>(0), false, &why);
        const Value stand(eng.hold("test.scene", std::make_shared<int>(0)));
        check(claimed == 0 && why.find("the scene kind is bound to another C++ type") == 0 &&
                  failedWith(eng.call("engine.node.setTransform", {stand, Value(eng.name("node", 1)), Value(Value::Object{})}),
                             "engine.node.setTransform: scene: " + std::to_string(stand.asInt()) +
                                 " is a test.scene handle, not a scene handle") &&
                  eng.hold("node", std::make_shared<int>(0)) == 0,
              "AN ENGINE KIND IS BOUND AT REGISTRATION: an int held as a scene is refused by name; a stand-in of the "
              "test's own kind FAILS where a scene is declared (never read); a named kind holds nothing (" + why + ")");
        Host early;
        early.setConsole([](const std::string &) {});
        early.registry().hold("scene", std::make_shared<int>(0));
        registerEngineCommands(early, nullptr, "no device here", false);
        const Result refused = early.run("host.check({ok: true, what: 'never'});\n", "early.js");
        check(!early.registry().validate().empty() &&
                  early.registry().validate()[0].find("scene: the kind is bound to another C++ type") == 0 &&
                  refused.status == Status::Failed,
              "a kind held as another type BEFORE the engine's registration is a registry FAULT and the host refuses "
              "every run (" + (early.registry().validate().empty() ? std::string("none") : early.registry().validate()[0]) +
                  ")");
        Registry forge;
        const std::int64_t held = forge.hold("thing", std::make_shared<int>(1));
        const std::int64_t minted = forge.name("thing", 1, &mint);
        forge.add({"test.forge", {Access::Changes, {}, Kind::Int}, "mints a thing handle from an id"},
                  [](Call &c) { return c.name("thing", 1); });
        check(held > 0 && minted == 0 &&
                  mint == "the thing kind is HELD: its handles are made by holding a thing, never minted from an id" &&
                  failedWith(forge.call("test.forge", {}), "test.forge: the thing kind is HELD") &&
                  eng.name("scene", 1) == 0,
              "A HANDLE IS MINTED ONLY BY THE DOOR THAT OWNS ITS KIND: an id minted under a held kind (it would name the "
              "table's slot 1 - the first held object: a forged handle) is refused by name (" + mint + ")");
    }
    {
        // ... and the binding itself: the first hold binds the kind; another type under it is refused by name (a
        // command's Call::hold FAILS with it); a read as another type answers null
        Registry typed;
        std::string why;
        const std::int64_t gadget = typed.hold("gadget", std::make_shared<int>(7));
        const std::int64_t other = typed.hold("gadget", std::make_shared<double>(2.5), false, &why);
        typed.add({"test.wrongType", {Access::Changes, {}, Kind::Int}, "holds a double as a gadget"},
                  [](Call &c) { return c.hold("gadget", std::make_shared<double>(1.0)); });
        check(gadget > 0 && other == 0 &&
                  why == "the gadget kind is bound to another C++ type (one kind, one type: a gadget handle names only "
                         "what its first hold named)" &&
                  failedWith(typed.call("test.wrongType", {}), "test.wrongType: the gadget kind is bound to another C++ type") &&
                  typed.object<int>(gadget) && *typed.object<int>(gadget) == 7 && !typed.object<double>(gadget),
              "A HELD KIND IS BOUND TO ONE C++ TYPE: a double under the int-bound kind is refused by name, at the registry "
              "and through a command; the int reads back as an int and never as a double (" + why + ")");
    }
    {
        // THE FLAT BATCH (engine.node.setTransforms, 2a a4): its shape is checked before any node moves - on the headless
        // engine's real scene: three live nodes, a fourth made and REMOVED (dead), and a call that passes every check
        // reaches the engine
        Registry &b = live;
        const Value scene = realScene;
        Value::Array nodes;
        for (int i = 0; i < 3; ++i) nodes.push_back(b.call("engine.node.create", {scene}).value);
        const Value dead = b.call("engine.node.create", {scene}).value;
        b.object<Scene>(scene.asInt())->removeNode(NodeId(Call::idOf(dead)));
        const auto numbersOf = [](std::size_t n, double x) { return Value(Value::Array(n, Value(x))); };
        const auto batch = [&](Value::Object o) { return b.call("engine.node.setTransforms", {scene, Value(std::move(o))}); };
        Value::Array withDead = nodes;
        withDead.push_back(dead);
        Value::Array tooMany(65537, nodes[0]);
        Value::Array nanAt7 = numbersOf(9, 1.0).asArray();
        nanAt7[7] = Value(std::numeric_limits<double>::quiet_NaN());
        check(failedWith(batch({{"nodes", Value(nodes)}, {"positions", numbersOf(8, 0)}}),
                         "engine.node.setTransforms: transforms.positions holds 8 numbers, not a multiple of 3") &&
                  failedWith(batch({{"nodes", Value(nodes)}, {"rotations", numbersOf(9, 0)}}),
                             "transforms.rotations holds 9 numbers, not a multiple of 4") &&
                  failedWith(batch({{"nodes", Value(nodes)}, {"scales", numbersOf(12, 1)}}),
                             "transforms.scales holds 12 numbers, not 3 x 3 nodes"),
              "setTransforms: a part whose length is not 3 n / 4 n / 3 n FAILS by name (no multiple of its stride: the "
              "declaration's check; a multiple for another count of nodes: the command's)");
        check(failedWith(batch({{"nodes", Value(nodes)}, {"positions", Value(nanAt7)}}),
                         "transforms.positions[7] is not a finite number") &&
                  failedWith(batch({{"nodes", Value(withDead)}}), "engine.node.setTransforms: nodes[3]: node " +
                                                                      std::to_string(Call::idOf(dead)) + " is not a node") &&
                  failedWith(batch({{"nodes", Value(tooMany)}}), "transforms.nodes holds 65537 elements, at most 65536"),
              "setTransforms: a NaN at positions[7], a dead node at nodes[3] and 65,537 nodes each FAIL by name");
        check(batch({{"nodes", Value(nodes)}}).status == Status::Ok &&
                  batch({{"nodes", Value(nodes)}, {"positions", numbersOf(9, 0)}, {"rotations", numbersOf(12, 0)},
                         {"scales", numbersOf(9, 1)}}).status == Status::Ok &&
                  signatureText(*b.find("engine.node.setTransforms")) ==
                      "engine.node.setTransforms(scene: target scene handle, transforms: object {nodes: node handle[] <= 65536, "
                      "positions: float[3n] <= 196608 numbers = default, rotations: float[4n] <= 262144 numbers = default, "
                      "scales: float[3n] <= 196608 numbers = default}) -> null [changes]",
              "THE CONTROL: every part absent, and every part whole, pass the checks and the real engine takes them; the "
              "signature: " +
                  signatureText(*b.find("engine.node.setTransforms")));
    }
    check(failedWith(eng.call("engine.scene.create", {Value("s")}), "engine.scene.create: the engine did not start: no device here") &&
              failedWith(eng.call("engine.frames", {Value(1)}), "the engine did not start: no device here"),
          "with no engine every engine command FAILS saying why the engine is absent");
    Value::Bytes px(4 * 2 * 4);
    for (std::size_t i = 0; i < px.size(); ++i) px[i] = std::uint8_t(i * 7);   // byte i = 7 i: a column steps 28 a channel
    const Result made4x2 =
        eng.call("engine.picture.create", {Value(Value::Object{{"width", Value(4)}, {"height", Value(2)}, {"rgba", Value::bytes(px)}})});
    const Value pic = made4x2.value["picture"];
    unsigned long long h = 14695981039346656037ull;
    for (std::uint8_t b : px) { h ^= b; h *= 1099511628211ull; }
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", h);
    check(made4x2.status == Status::Ok && made4x2.value["width"] == Value(4) && made4x2.value["height"] == Value(2) &&
              eng.call("engine.picture.hash", {pic}).value == Value(hex),
          std::string("PICTURES BY HANDLE: engine.picture.create answers {picture, width, height}; engine.picture.hash is the "
                      "FNV-1a 64 of the bytes, read in place (") + hex + ")");
    const auto span = [](const char *axis, int a, int b, int from, int to) {
        return Value(Value::Object{{"axis", Value(axis)}, {"a", Value(a)}, {"b", Value(b)}, {"from", Value(from)}, {"to", Value(to)}});
    };
    // column 3 against column 2: every byte 4 apart = 28 codes; row 1 against row 0: 16 bytes apart = 112 codes
    check(eng.call("engine.picture.columnStep", {pic, span("column", 3, 2, 0, 2)}).value == Value(28.0) &&
              eng.call("engine.picture.columnStep", {pic, span("row", 1, 0, 1, 3)}).value == Value(112.0),
          "engine.picture.columnStep: a column step of 4 bytes = 28 codes, a row step of 16 bytes = 112 codes");
    const Value rect(Value::Object{{"x0", Value(1)}, {"y0", Value(0)}, {"x1", Value(3)}, {"y1", Value(2)}});
    // pixels (1,0) (2,0) (1,1) (2,1) = byte bases 4, 8, 20, 24: the red mean 7 * 14 = 98, each channel one step up
    check(eng.call("engine.picture.mean", {pic, rect}).value ==
              Value(Value::Array{Value(98.0), Value(105.0), Value(112.0), Value(119.0)}),
          "engine.picture.mean over [1,3) x [0,2) = [98, 105, 112, 119]");
    check(eng.call("engine.picture.bytes", {pic}).value == Value::bytes(px),
          "engine.picture.bytes copies the picture into the script only when asked (the same bytes)");
    check(failedWith(eng.call("engine.picture.columnStep", {pic, span("column", 4, 2, 0, 2)}), "outside the picture") &&
              failedWith(eng.call("engine.picture.create", {Value(Value::Object{{"width", Value(4)}, {"height", Value(3)},
                                                                                {"rgba", Value::bytes(px)}})}),
                         "not width x height x 4"),
          "a span outside the picture and bytes that are not width x height x 4 FAIL");
    check(eng.call("engine.picture.release", {pic}).value == Value(true) &&
              failedWith(eng.call("engine.picture.hash", {pic}), "is stale (its picture was released)"),
          "a released picture's handle is STALE by name");
    {
        // THE CAP (the host's option, Host::Options::pictureBytesCap; 512 MiB by default): a host of a 16-byte cap -
        // two 1 x 2 pictures (8 bytes each) fill it EXACTLY (the bound is in); one more pixel FAILS by name; one
        // released, it is taken (the cap counts the LIVE pictures)
        Host::Options small;
        small.pictureBytesCap = 16;
        Host capped(small);
        registerEngineCommands(capped, nullptr, "no device here", false);
        Registry &cr = capped.registry();
        const auto make = [&cr](int w, int h) {
            return cr.call("engine.picture.create", {Value(Value::Object{{"width", Value(w)}, {"height", Value(h)},
                                                                        {"rgba", Value::bytes(Value::Bytes(size_t(w * h * 4), 9))}})});
        };
        const Result p1 = make(1, 2), p2 = make(1, 2), p3 = make(1, 1);
        const bool full = p1.status == Status::Ok && p2.status == Status::Ok &&
                          failedWith(p3, "would hold 20 bytes, over the host's cap 16: release one first");
        cr.call("engine.picture.release", {p1.value["picture"]});
        const Result p4 = make(1, 1);
        check(full && p4.status == Status::Ok && Host().options().pictureBytesCap == (std::size_t(512) << 20),
              "THE CAP is the host's option (512 MiB by default): past it a picture FAILS by name; a release makes room (" +
                  p3.message.substr(0, 80) + ")");
    }
    {
        // A PICTURE LIVES ACROSS CALLS AND RUNS until released (MCP-ON-HOST-1); jahengine releases the scoped ones at its
        // script's end (Registry::releaseScoped)
        Host live;
        live.setConsole([](const std::string &) {});
        registerEngineCommands(live, nullptr, "no device here", false);
        const Result r1 = live.run("globalThis.p = engine.picture.create({width: 1, height: 1, rgba: new Uint8Array([1, 2, 3, 4])});\n"
                                   "host.check({ok: p.width === 1, what: 'made'});\n", "make.js");
        const Result r2 = live.run("host.check({ok: engine.picture.hash(p.picture).length === 16, what: 'still held'});\n",
                                   "later.js");
        live.registry().releaseScoped();
        const Result r3 = live.run("engine.picture.hash(p.picture);\n", "after.js");
        check(r1.status == Status::Ok && r2.status == Status::Ok && live.checksFailed() == 0 && r3.status == Status::Failed &&
                  r3.message.find("is stale") != std::string::npos,
              "a picture handle lives across runs until released; the host's end-of-script release makes it stale");
    }

    // 6. THE ROUTE DECLARATION's JUDGEMENT (engine.view.declareRoutes; RouteDeclaration.h): exactly requireRoutes' law
    {
        RouteWords decl;
        decl.integrate = decl.reflection = decl.store = true;
        RouteWords ran = decl;
        ran.store = false;   // the chain holds, the store not live yet
        std::string line;
        const bool waits = judgeRoutes("v", decl, ran, true, ran, 1, line) == RouteJudgement::Wait &&
                           judgeRoutes("v", decl, ran, true, ran, inspect::kRouteProductFrames - 1, line) ==
                               RouteJudgement::Wait;
        const bool late = judgeRoutes("v", decl, ran, true, ran, inspect::kRouteProductFrames, line) == RouteJudgement::Broken &&
                          line.find("MISMATCH") != std::string::npos;
        check(waits && late, "a declared product not live waits while the chain holds, and is BROKEN on frame " +
                                 std::to_string(inspect::kRouteProductFrames));
        ran.store = true;
        check(judgeRoutes("v", decl, ran, true, ran, 3, line) == RouteJudgement::Held &&
                  line == "ROUTES v: declared {integrate reflection store}, the frame ran {integrate reflection store} "
                          "(frame 3 after the declaration)",
              "the declaration held prints both sides: " + line);
        RouteWords gathered = ran;
        gathered.gather = true;
        gathered.integrate = false;
        check(judgeRoutes("v", decl, gathered, true, gathered, 1, line) == RouteJudgement::Broken &&
                  line.find("declared {integrate reflection store}, the frame ran {gather reflection store}") != std::string::npos,
              "a chain other than the declared one is BROKEN on the first frame, both sides named");
        RouteWords pub = ran;
        pub.integrate = false;
        check(judgeRoutes("v", decl, ran, true, pub, 1, line) == RouteJudgement::Broken &&
                  line.find("PUBLISHED routes {reflection} differ from the engine's own {integrate reflection}") !=
                      std::string::npos &&
                  judgeRoutes("v", decl, ran, false, pub, 1, line) == RouteJudgement::Held,
              "THE TWO DOORS: a published row unlike the engine's own record is BROKEN by name (no published row yet: "
              "the engine's own record judges)");
        RouteWords paused = ran;
        paused.paused = true;
        check(judgeRoutes("v", decl, paused, true, paused, 1, line) == RouteJudgement::Broken,
              "GI paused when the declaration did not say paused is BROKEN");
    }
    // THE VERDICT's JUDGES: a judgement registered with the host runs before the count, its law a check
    {
        Host judged;
        judged.setConsole([](const std::string &) {});
        judged.atVerdict([&judged]() { judged.law(false, "a declaration no frame asserted"); });
        const Result r = judged.run("host.check({ok: true, what: 'one law held'});\n", "judged.js");
        check(r.status == Status::Ok && judged.verdict(r) == 1 && judged.checksRun() == 2,
              "a judge the verdict runs fails it by name (a pending route declaration at the script's end)");
    }

    // 7. THE REGISTRY ENFORCES ITSELF (the final read's W4): a host whose registry refused a command does not start; a
    //    second engine registered into one host is refused by name; a command's C++ exception is a Failed call
    {
        Host broken;
        broken.setConsole([](const std::string &) {});
        const bool refused = !broken.registry().add({"test.bare", {Access::Reads, {}, Kind::Null}, ""}, [](Call &) { return Value(); });
        const Result r = broken.run("host.check({ok: true, what: 'never'});\n", "broken.js");
        check(refused && r.status == Status::Failed && r.message.find("the host did not start") == 0 &&
                  r.message.find("test.bare: no doc") != std::string::npos && broken.checksRun() == 0,
              "a host whose registry refused a command refuses to run, naming it: " + r.message.substr(0, r.message.find('\n')));
        Host two;
        two.setConsole([](const std::string &) {});
        registerEngineCommands(two, nullptr, "first", false);
        const bool clean = two.registry().validate().empty();
        registerEngineCommands(two, nullptr, "second", false);
        const Result r2 = two.run("host.check({ok: true, what: 'never'});\n", "two.js");
        check(clean && r2.status == Status::Failed && r2.message.find("engine.scene.create: registered twice") != std::string::npos,
              "a second engine's commands registered into one host are refused by name and the host does not start");
    }
    {
        Host thrower;
        std::vector<std::string> said;
        thrower.setConsole([&said](const std::string &l) { said.push_back(l); });
        thrower.registry().add({"test.throws", {Access::Reads, {}, Kind::Null}, "throws a std::exception"},
                               [](Call &) -> Value { throw std::runtime_error("boom"); });
        thrower.registry().add({"test.throwsOdd", {Access::Reads, {}, Kind::Null}, "throws an int"}, [](Call &) -> Value { throw 42; });
        thrower.registry().add({"test.checked", {Access::Reads, {target(handle("thing", "gadget"))}, Kind::Null}, "an existence check throws"},
                               [](Call &) { return Value(); });
        thrower.registry().checkKind("gadget", [](const Call &, std::uint32_t, std::string &) -> bool {
            throw std::runtime_error("the engine's read threw");
        });
        const Result inCheck = thrower.registry().call("test.checked", {Value(thrower.registry().name("gadget", 3))});
        const Result direct = thrower.registry().call("test.throws", {});
        const Result odd = thrower.registry().call("test.throwsOdd", {});
        const Result js = thrower.run("try { test.throws(); } catch (e) { console.log(e.message); }\ntest.throwsOdd();\n",
                                      "throws.js");
        check(failedWith(inCheck, "test.checked: threw: the engine's read threw"),
              "an existence check's exception is a Failed call by name too (the try covers the checks)");
        check(failedWith(direct, "test.throws: threw: boom") && failedWith(odd, "not a std::exception") &&
                  !said.empty() && said[0] == "test.throws: threw: boom" && js.status == Status::Failed &&
                  js.message.find("throws.js:2") != std::string::npos,
              "a command's C++ exception is a Failed call by name - catchable in JS, an uncaught one ends the run with "
              "its line (never terminate with no verdict)");
    }

    // 8. TYPED HANDLES (the final read's W1; the lead's S1): a handle carries its owner and its kind; a held one goes
    //    stale when released; a named one (the engine's own id) is asked of its family's existence check
    {
        Registry a, other;
        const auto make = [](Registry &r) {
            r.add({"test.make", {Access::Changes, {arg("kind", Kind::String)}, Kind::Int}, "a held int of that kind -> handle"},
                  [](Call &c) { return c.hold(c[0].asString(), std::make_shared<int>(7)); });
        };
        make(a);
        make(other);
        a.add({"test.use", {Access::Reads, {target(handle("scene", "scene"))}, Kind::Int}, "the held int"},
              [](Call &c) { return Value(*c.object<int>(c[0])); });
        a.add({"test.free", {Access::Changes, {target(handle("scene", "scene"))}, Kind::Bool}, "releases it"},
              [](Call &c) { return Value(c.registry().release(c[0].asInt())); });
        a.add({"test.named", {Access::Reads, {target(handle("scene", "scene")), object("o", {handle("node", "node")})}, Kind::Int},
               "the node's engine id"},
              [](Call &c) { return Value(std::int64_t(Call::idOf(c[1]["node"]))); });
        a.add({"test.mint", {Access::Reads, {arg("id", Kind::Int)}, Kind::Int, "node"}, "a node handle of that id"},
              [](Call &c) { return c.name("node", (unsigned long long)c[0].asInt()); });
        a.checkKind("node", [](const Call &, std::uint32_t id, std::string &why) {
            if (id == 5) return true;
            why = "node " + std::to_string(id) + " is not a node of the scene";
            return false;
        });
        const Value scene = a.call("test.make", {Value("scene")}).value, view = a.call("test.make", {Value("view")}).value;
        const Value foreign = other.call("test.make", {Value("scene")}).value;
        const std::int64_t hs = scene.asInt();
        check(a.validate().empty() && hs > 0 && hs < (std::int64_t(1) << 53) && a.call("test.use", {scene}).value == Value(7),
              "a held handle is a plain script number (< 2^53) and names its object");
        check(failedWith(a.call("test.use", {view}), "is a view handle, not a scene handle") &&
                  failedWith(a.call("test.use", {Value(1)}), "1 is no scene handle of this host") &&
                  failedWith(a.call("test.use", {foreign}), "is no scene handle of this host"),
              "a view handle for a scene, a plain number, and ANOTHER HOST's scene handle each FAIL by name");
        check(a.call("test.free", {scene}).value == Value(true) &&
                  failedWith(a.call("test.use", {scene}), "is stale (its scene was released)"),
              "a released handle is STALE by name");
        const Value scene2 = a.call("test.make", {Value("scene")}).value;
        const Value node5 = a.call("test.mint", {Value(5)}).value, node6 = a.call("test.mint", {Value(6)}).value;
        const Value mesh(a.name("mesh", 5));
        const auto named = [&](const Value &n) { return a.call("test.named", {scene2, Value(Value::Object{{"node", n}})}); };
        check(named(node5).value == Value(5) && failedWith(named(node6), "test.named: node: node 6 is not a node of the scene"),
              "a NAMED handle carries the engine's id; its existence check FAILS a dead or foreign id by name");
        check(failedWith(named(mesh), "is a mesh handle, not a node handle"),
              "a mesh handle where a node is declared FAILS by name (the kind is in the bits, no lookup)");
        // AN ARRAY OF HANDLES (handles(); 2a a2): every element kind-checked AND asked of its kind's existence check
        a.add({"test.many", {Access::Reads, {target(handle("scene", "scene")), object("o", {handles("nodes", "node").upTo(8)})},
                             Kind::Int}, "how many nodes"},
              [](Call &c) { return Value(std::int64_t(c[1]["nodes"].asArray().size())); });
        const auto many = [&](Value::Array nodes) {
            return a.call("test.many", {scene2, Value(Value::Object{{"nodes", Value(std::move(nodes))}})});
        };
        check(many({node5, node5, node5}).value == Value(3) &&
                  failedWith(many({node5, node5, node5, node6}), "test.many: nodes[3]: node 6 is not a node of the scene") &&
                  failedWith(many({node5, mesh}), "o.nodes[1]: " + std::to_string(mesh.asInt()) +
                                                      " is a mesh handle, not a node handle"),
              "an array of handles: three live nodes pass; a DEAD node at nodes[3] FAILS naming nodes[3]; a mesh among "
              "the nodes FAILS naming its index");
        {
            Registry counted;
            int asked = 0;
            counted.add({"test.many", {Access::Reads, {target(handle("scene", "scene")), object("o", {handles("nodes", "node").upTo(8)})},
                                       Kind::Int}, "how many nodes"},
                        [](Call &c) { return Value(std::int64_t(c[1]["nodes"].asArray().size())); });
            counted.checkKind("node", [&asked](const Call &, std::uint32_t, std::string &) { ++asked; return true; });
            const Value s(counted.hold("scene", std::make_shared<int>(0)));
            Value::Array seven;
            for (int i = 1; i <= 7; ++i) seven.push_back(Value(counted.name("node", std::uint32_t(i))));
            const Result r7 = counted.call("test.many", {s, Value(Value::Object{{"nodes", Value(seven)}})});
            check(r7.value == Value(7) && asked == 7 &&
                      signatureText(*counted.find("test.many")) ==
                          "test.many(scene: target scene handle, o: object {nodes: node handle[] <= 8}) -> int [reads]",
                  "the existence check is asked once for EACH of 7 elements (" + std::to_string(asked) +
                      " asked); the signature prints node handle[] <= 8");
        }
        check(failedWith(a.call("test.mint", {Value(std::int64_t(1) << 32 | 5)}), "does not fit a handle's 32 bits"),
              "an engine id over 2^32 - 1 is refused at minting (2^32 + 5 never becomes 5)");
        check(signatureText(*a.find("test.named")) == "test.named(scene: target scene handle, o: object {node: node handle}) -> int [reads]" &&
                  signatureText(*a.find("test.mint")) == "test.mint(id: int) -> node handle [reads]",
              "the signature prints the handles' kinds: " + signatureText(*a.find("test.named")));
        Host jsHost;
        jsHost.setConsole([](const std::string &) {});
        make(jsHost.registry());
        jsHost.registry().add({"test.use", {Access::Reads, {target(handle("scene", "scene"))}, Kind::Int}, "the held int"},
                              [](Call &c) { return Value(*c.object<int>(c[0])); });
        const Result js = jsHost.run("const s = test.make('scene'), v = test.make('view');\n"
                                     "host.check({ok: test.use(s) === 7, what: 'a scene handle through JS'});\n"
                                     "test.use(v);\n", "handles.js");
        check(js.status == Status::Failed && jsHost.checksFailed() == 0 && js.message.find("handles.js:3") != std::string::npos &&
                  js.message.find("is a view handle, not a scene handle") != std::string::npos,
              "through JS a handle is a number that keeps its kind: the wrong kind FAILS with the script's line");
        // the engine's own engine.view.setScene: its bound scene is DECLARED a scene handle, so the kind check above
        // refuses a view there before the command runs (a headless engine makes no offscreen view: the declaration is
        // the proof; it bound the wrong object silently before 2a)
        check(signatureText(*live.find("engine.view.setScene")) ==
                  "engine.view.setScene(view: target view handle, bind: object {scene: scene handle}) -> bool [changes]",
              "engine.view.setScene declares its scene a scene handle: " + signatureText(*live.find("engine.view.setScene")));
    }

    // 9. THE SIGNATURE DECLARES ITS DOMAIN (the lead's S2): choices, ranges and units are the signature's - checked by
    //    the host before the command and printed for the docs; a number is finite unless declared otherwise (its bits)
    {
        Registry d;
        Value seen;
        d.add({"test.domain",
               {Access::Reads, {object("o", {arg("mode", Kind::String, Value("vct")).oneOf({"off", "vct"}),
                              arg("width", Kind::Int, Value(1)).range(1, 16384).in("px"),
                              arg("x", Kind::Number, Value(0.0)), arg("wild", Kind::Number, Value(0.0)).allowNonFinite(),
                              numbers("v", 3, Value(Value::Array{Value(0), Value(0), Value(0)})),
                              arg("big", Kind::Int, Value(0))})},
                Kind::Null},
               "a declared domain"},
              [&seen](Call &c) { seen = c[0]; return Value(); });
        const auto call = [&](const char *k, Value v) { return d.call("test.domain", {Value(Value::Object{{k, std::move(v)}})}); };
        const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
        check(failedWith(call("mode", Value("vctPcc")), "o.mode 'vctPcc' is not one of \"off\"|\"vct\"") &&
                  call("mode", Value("off")).status == Status::Ok,
              "a String outside its declared choices FAILS by name, listing them");
        check(failedWith(call("width", Value(0)), "o.width 0 is outside 1..16384 px") &&
                  failedWith(call("width", Value(16385)), "o.width 16385 is outside 1..16384 px") &&
                  call("width", Value(16384)).status == Status::Ok,
              "an Int outside its declared range FAILS by name (with its unit); the bound itself is in");
        check(failedWith(call("x", Value(nan)), "o.x is not a finite number") &&
                  failedWith(call("x", Value(-inf)), "o.x is not a finite number") &&
                  failedWith(call("v", Value(Value::Array{Value(inf), Value(0), Value(0)})), "o.v[0] is not a finite number") &&
                  call("wild", Value(nan)).status == Status::Ok,
              "NaN and infinity FAIL by their bits (a vector's element too) unless the parameter declares them allowed");
        check(failedWith(call("big", Value(1e17)), "o.big 100000000000000000 is beyond 2^53") &&
                  call("big", Value(std::int64_t(100000000000000000))).status == Status::Ok &&
                  failedWith(call("big", Value(nan)), "o.big must be an integer"),
              "an integral Number beyond 2^53 is no exact integer and FAILS for an Int (an Int value - a BigInt - is taken)");
        check(signatureText(*d.find("test.domain")).find("mode: string \"off\"|\"vct\" = default") != std::string::npos &&
                  signatureText(*d.find("test.domain")).find("width: int 1..16384 px = default") != std::string::npos &&
                  signatureText(*d.find("test.domain")).find("wild: number (non-finite allowed)") != std::string::npos,
              "the signature prints the domain (the generated docs): " + signatureText(*d.find("test.domain")));
        check(signatureText(*eng.find("engine.gi.set")).find("mode: string \"off\"|\"vct\"|\"vctPccHybrid\"") !=
                      std::string::npos &&
                  signatureText(*eng.find("engine.frames")) == "engine.frames(n: int >= 0 frames) -> null [changes]",
              "the engine's commands declare theirs: " + signatureText(*eng.find("engine.frames")));
        check(failedWith(live.call("engine.node.setTransform",
                                  {realScene, realNode, Value(Value::Object{{"position", Value(Value::Array{Value(nan), Value(0), Value(0)})}})}),
                         "transform.position[0] is not a finite number"),
              "setTransform({position: [NaN, 0, 0]}) FAILS before the engine is asked (it reached setNodeTransform before)");

        // AN ARRAY's MAXIMUM COUNT and AN EXCLUSIVE MINIMUM (2a a2): declared, checked by the host, printed
        d.add({"test.bounded",
               {Access::Reads,
                {object("o", {arg("v", Kind::Array, Value(Value::Array{})).upTo(3),
                              arg("pos", Kind::Number, Value(1.0)).above(0).in("m")})},
                Kind::Null},
               "a bounded array and a positive number"},
              [](Call &) { return Value(); });
        const auto bounded = [&](const char *k, Value v) { return d.call("test.bounded", {Value(Value::Object{{k, std::move(v)}})}); };
        const Value three(Value::Array{Value(1), Value(2), Value(3)}), four(Value::Array{Value(1), Value(2), Value(3), Value(4)});
        check(bounded("v", three).status == Status::Ok && failedWith(bounded("v", four), "o.v holds 4 elements, at most 3") &&
                  bounded("pos", Value(0.001)).status == Status::Ok &&
                  failedWith(bounded("pos", Value(0)), "o.pos 0 is outside > 0 m") &&
                  signatureText(*d.find("test.bounded")) ==
                      "test.bounded(o: object {v: number[] <= 3 = default, pos: number > 0 m = default}) -> null [reads]",
              "maxCount: 3 elements pass, 4 FAIL by name; an exclusive minimum: 0.001 passes, 0 FAILS; printed: " +
                  signatureText(*d.find("test.bounded")));

        // engine.gi.set's CASCADES HOLD THE GI's OWN LAW (L4; PhotonComponent::resolveCascadeTable): each bound FAILS by
        // name BEFORE the engine is asked (no engine here: a table that passes every bound reaches "did not start")
        const auto cas = [](double hs, int res, double step) {
            return Value(Value::Object{{"halfSize", Value(hs)}, {"resolution", Value(res)}, {"stepCells", Value(step)}});
        };
        const auto giSet = [&](Value::Array table) {
            return live.call("engine.gi.set", {realScene, Value(Value::Object{{"mode", Value("vct")},
                                                                             {"cascades", Value(std::move(table))}})});
        };
        const struct { Value::Array table; const char *names; } refused[] = {
            {{cas(1, 64, 0), cas(2, 64, 0), cas(4, 64, 0), cas(8, 64, 0), cas(16, 64, 0)}, "params.cascades holds 5 elements, at most 4"},
            {{cas(32, 8, 0)}, "params.cascades[0].resolution 8 is outside 16..256 cells"},
            {{cas(32, 264, 0)}, "params.cascades[0].resolution 264 is outside 16..256 cells"},
            {{cas(32, 20, 0)}, "params.cascades[0].resolution 20 is not a multiple of 8 (the engine would build 24"},
            {{cas(0, 64, 0)}, "params.cascades[0].halfSize 0 is outside > 0 m"},
            {{cas(1e-50, 64, 0)}, "params.cascades[0].halfSize is no float above 0"},
            {{cas(32, 64, -1)}, "params.cascades[0].stepCells -1 is outside 0..128 cells"},
            {{cas(32, 64, 0.5)}, "params.cascades[0].stepCells must be 0 (the engine derives it) or 1..32 cells"},
            {{cas(32, 64, 33)}, "params.cascades[0].stepCells must be 0 (the engine derives it) or 1..32 cells"},
            {{cas(32, 64, 0), cas(16, 64, 0)}, "params.cascades[1] does not grow outward"},
            {{cas(16, 64, 0), cas(32, 256, 0)}, "params.cascades[1] does not grow outward"},
        };
        std::string missed;
        for (const auto &x : refused) {
            const Result r = giSet(x.table);
            if (!failedWith(r, std::string("engine.gi.set: ") + x.names)) missed += "\n      " + std::string(x.names) + " <- " + r.message;
        }
        check(missed.empty(), "engine.gi.set: each of the GI's 11 cascade bounds FAILS by name" + missed);
        check(giSet({cas(8, 64, 1), cas(32, 64, 32)}).status != Status::Failed && giSet({}).status != Status::Failed &&
                  signatureText(*eng.find("engine.gi.set")).find("cascades: object[] <= 4 {halfSize: number > 0 m, resolution: "
                                                                  "int 16..256 cells, stepCells: number 0..128 cells = default}") !=
                      std::string::npos,
              "THE CONTROL: a table inside every bound (and none) passes them to the real engine; the bounds are printed");
    }

    // 10. THE LANGUAGE TABLE (the lead's S5): a script's language is its name's extension; a SECOND LANGUAGE is one small
    //     interface implemented in one place - here a dummy one, registered from this file: one command name a line,
    //     called through Host::call - and the host runs it with nothing else touched
    {
        struct Lines final : Host::Language {
            explicit Lines(Host &h) : host(h) {}
            Result run(const std::string &source, const std::string &) override
            {
                Result r;
                size_t a = 0;
                while (a < source.size()) {
                    const size_t b = source.find('\n', a);
                    const std::string line = source.substr(a, b == std::string::npos ? std::string::npos : b - a);
                    a = b == std::string::npos ? source.size() : b + 1;
                    if (line.empty()) continue;
                    const Result c = host.call(line, {});
                    if (c.status == Status::Failed) return c;
                }
                return r;
            }
            Host &host;
        };
        Host two;
        int pings = 0;
        two.registry().add({"test.ping", {Access::Changes, {}, Kind::Null}, "counts a ping"}, [&pings](Call &) { ++pings; return Value(); });
        two.addLanguage(".lines", [](Host &h) { return std::unique_ptr<Host::Language>(new Lines(h)); });
        const Result byLines = two.run("test.ping\ntest.ping\n", "two.lines");
        const Result byJs = two.run("test.ping();\n", "one.js");
        const Result none = two.run("test.ping\n", "x.lua");
        check(byLines.status == Status::Ok && byJs.status == Status::Ok && pings == 3 && none.status == Status::Failed &&
                  none.message.find("no script language reads 'x.lua' (this host reads: *.js *.lines)") == 0,
              "a second language registered from a test file runs beside JavaScript; an unknown extension FAILS by name (" +
                  none.message + ")");
    }

    // 11. NO WALL CLOCK AND NO UNSEEDED RANDOM IN A SCRIPT (the re-read's C1; CODING_RULES § 5: tests count frames): the
    //     realm has no Date and no performance; Math.random is a FIXED-SEED stream (Host::Options::randomSeed, a fixed
    //     default) - the same sequence every run, in every host, unless a host states another seed
    {
        const char *draw = "console.log(typeof Date, typeof performance, Math.random(), Math.random(), Math.random());\n"
                           "host.check({ok: true, what: 'drawn'});\n";
        std::vector<std::string> a, b, c;
        Host one, two;
        one.setConsole([&a](const std::string &l) { a.push_back(l); });
        two.setConsole([&b](const std::string &l) { b.push_back(l); });
        const Result r1 = one.run(draw, "clock1.js"), r2 = one.run(draw, "clock2.js"), r3 = two.run(draw, "clock3.js");
        Host::Options seeded;
        seeded.randomSeed = 12345;
        Host other(seeded);
        other.setConsole([&c](const std::string &l) { c.push_back(l); });
        const Result r4 = other.run(draw, "clock4.js");
        const bool ran = r1.status == Status::Ok && r2.status == Status::Ok && r3.status == Status::Ok &&
                         r4.status == Status::Ok && a.size() == 4 && b.size() == 2 && c.size() == 2;
        check(ran && a[0].rfind("undefined undefined ", 0) == 0,
              "the realm has NO wall clock: typeof Date and typeof performance are 'undefined' (" +
                  (a.empty() ? std::string("?") : a[0]) + ")");
        check(ran && a[0] == a[2] && a[0] == b[0] && c[0] != a[0],
              "Math.random is a FIXED-SEED stream: two runs and two hosts draw the same sequence; a host that states "
              "another seed draws another (" + (c.empty() ? std::string("?") : c[0]) + ")");
    }

    // 12. THE CALL BY NAMED ARGUMENTS (MCP-ON-HOST-1 stage 1: Registry::callNamed - the adapter knows no command): the
    //     members go to the signature's POSITIONS (never the object's order), and every strict law holds through it in
    //     the same words as the positional call
    {
        const auto same = [](const Result &a, const Result &b) {
            return a.status == b.status && a.message == b.message && a.value == b.value;
        };
        Registry nr;
        nr.add({"test.pair",
                {Access::Reads,
                 {target(handle("scene", "scene")), target(handle("node", "node")),
                  object("opts", {arg("x", Kind::Number, Value(1.0))}, Value(Value::Object{}))},
                 Kind::Array},
                "the scene, the node and opts.x, in that order"},
               [](Call &c) { return Value(Value::Array{c[0], c[1], c[2]["x"]}); });
        const Value scene(nr.hold("scene", std::make_shared<int>(1))), node(nr.name("node", 5));
        const Value opts(Value::Object{{"x", Value(2.0)}});
        const Result positional = nr.call("test.pair", {scene, node, opts});
        const Result inOrder = nr.callNamed("test.pair", {{"scene", scene}, {"node", node}, {"opts", opts}});
        const Result reversed = nr.callNamed("test.pair", {{"opts", opts}, {"node", node}, {"scene", scene}});
        check(positional.status == Status::Ok && same(inOrder, positional) && same(reversed, positional) &&
                  reversed.value == Value(Value::Array{scene, node, Value(2.0)}),
              "callNamed puts each member at its parameter's position: the two targets named in either order give the "
              "same call as the positional one (" + reversed.message + ")");
        check(same(nr.callNamed("test.pair", {{"node", node}, {"scene", scene}}), nr.call("test.pair", {scene, node})),
              "an absent member is null: its parameter's default, as the positional call's absent argument");
        const Result misspelt = nr.callNamed("test.pair", {{"scene", scene}, {"nodee", node}});
        const Result unknown = nr.callNamed("test.pair", {{"scene", scene}, {"node", node}, {"colour", Value(1)}});
        const std::string sig = "(" + signatureText(*nr.find("test.pair")) + ")";
        check(failedWith(misspelt, "test.pair: has no parameter 'nodee' " + sig) &&
                  failedWith(unknown, "test.pair: has no parameter 'colour' " + sig),
              "a misspelt member and an unknown parameter FAIL by name with the signature (" + misspelt.message + ")");
        check(failedWith(nr.callNamed("test.pair", {{"scene", scene}}), "test.pair: node is required") &&
                  same(nr.callNamed("test.pair", {{"scene", scene}}), nr.call("test.pair", {scene})),
              "a missing required parameter FAILS 'is required', in the positional call's words");
        check(failedWith(nr.callNamed("test.pairs", {}), "test.pairs is not a command") &&
                  same(nr.callNamed("test.pairs", {}), nr.call("test.pairs", {})),
              "an unknown command FAILS in the positional call's words");
        check(failedWith(nr.callNamed("test.pair", {{"scene", scene}, {"scene", scene}, {"node", node}}),
                         "test.pair: the parameter 'scene' is named twice"),
              "a parameter named twice FAILS by name");
        const auto both = [&](const Value &s, const Value &n, const Value &o, const std::string &part) {
            const Result named = nr.callNamed("test.pair", {{"scene", s}, {"node", n}, {"opts", o}});
            return failedWith(named, part) && same(named, nr.call("test.pair", {s, n, o}));
        };
        const Value stale(nr.hold("scene", std::make_shared<int>(2)));
        nr.release(stale.asInt());
        check(both(scene, Value(5.5), opts, "node must be an integer") &&
                  both(node, node, opts, "is a node handle, not a scene handle") &&
                  both(stale, node, opts, "is stale (its scene was released)") &&
                  both(scene, node, Value(Value::Object{{"y", Value(1)}}), "opts has no member 'y'") &&
                  both(scene, node, Value(Value::Object{{"x", Value("1")}}), "opts.x must be number"),
              "a non-integral Int, a wrong-kind handle, a dead handle, an undeclared member and a wrong kind FAIL through "
              "callNamed with the positional call's text");
        // THE THREE REAL SHAPES (the engine's commands, no engine): the named call is the positional call, word for word.
        // (`es` is a REAL scene of the headless engine handed into the engine-less host - an engine kind holds only its
        // type, SCRIPT-IN-ENGINE-1 2b - and the node is a MESH handle, refused by its kind bits at the node's position.)
        Scene *named = headless->createScene("mcp-named");
        const Value es(eng.hold("scene", std::shared_ptr<Scene>(named, [](Scene *) {}))), en(eng.name("mesh", 7));
        const Value tr(Value::Object{{"position", Value(Value::Array{Value(1), Value(2), Value(3)})}});
        const Value gi(Value::Object{{"mode", Value("vct")}, {"cascades", Value(Value::Array{})}});
        const Result f1 = engHost.callNamed("engine.frames", {{"n", Value(3)}}), f2 = engHost.call("engine.frames", {Value(3)});
        const Result t1 = eng.callNamed("engine.node.setTransform", {{"transform", tr}, {"node", en}, {"scene", es}});
        const Result t2 = eng.call("engine.node.setTransform", {es, en, tr});
        const Result g1 = eng.callNamed("engine.gi.set", {{"params", gi}, {"scene", es}});
        const Result g2 = eng.call("engine.gi.set", {es, gi});
        check(same(f1, f2) && same(t1, t2) && same(g1, g2) && f1.message.find("the engine did not start") != std::string::npos &&
                  g1.message.find("the engine did not start") != std::string::npos &&
                  t1.message.find("engine.node.setTransform: node: ") == 0 &&
                  t1.message.find("is a mesh handle, not a node handle") != std::string::npos,
              "engine.frames, engine.node.setTransform and engine.gi.set by name (members in any order) are their "
              "positional calls word for word (" + t1.message + ")");
        headless->destroyScene(named);
    }

    // 12a. AN ARRAY's LENGTH IS JUDGED BEFORE IT IS CONVERTED (SCRIPT-IN-ENGINE-1 2b; the 2a final read's L1): a script's
    //     `a.length = 4e9` costs it nothing, and converting it would cost the host 4e9 values (a bad_alloc through the
    //     JS engine's C frames: the process ends). THE LAW: the length is checked against the declaration - its count or
    //     its maximum - BEFORE any element is converted or any room reserved; an array the declaration does not take
    //     there (an argument too many, an undeclared member) is never read; each is a FAIL of that call by name, and the
    //     host lives on. THE CONTROL: the next call in the same host reaches its command (reached = 1, its answer 4)
    {
        Host capped;
        std::vector<std::string> said;
        capped.setConsole([&said](const std::string &l) { said.push_back(l); });
        int reached = 0;
        capped.registry().add({"test.capped",
                               {Access::Reads, {object("o", {arg("v", Kind::Array, Value(Value::Array{})).upTo(4)})}, Kind::Int},
                               "how many numbers (at most 4)"},
                              [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0]["v"].asArray().size())); });
        registerEngineCommands(capped, nullptr, "no device here", false);
        const std::string scene = "1";   // (any number: the conversion FAILS the call before the registry reads it)
        const Result r = capped.run(
            "const huge = []; huge.length = 4e9;\n"
            "const caught = (f) => { try { f(); return 'no failure'; } catch (e) { return e.message; } };\n"
            "console.log(caught(() => test.capped({v: huge})));\n"
            "console.log(caught(() => test.capped({v: [1, 2, 3, 4, 5]})));\n"
            "console.log(caught(() => test.capped({v: [1]}, huge)));\n"
            "console.log(caught(() => test.capped({v: [1], w: huge})));\n"
            "console.log(caught(() => engine.node.setTransforms(" + scene + ", {nodes: huge})));\n"
            "console.log(caught(() => engine.node.setTransforms(" + scene + ", {nodes: Array(65537).fill(1)})));\n"
            "let deep = 0, deepObject = {};\n"
            "for (let i = 0; i < 100000; ++i) { deep = [deep]; deepObject = {a: deepObject}; }\n"
            "console.log(caught(() => test.capped({v: deep})));\n"
            "console.log(caught(() => test.capped(deepObject)));\n"
            "console.log(test.capped({v: [1, 2, 3, 4]}));\n",
            "capped.js");
        const char *want[] = {"test.capped: argument 1: o.v holds 4000000000 elements, at most 4",
                              "test.capped: argument 1: o.v holds 5 elements, at most 4",
                              "test.capped: 2 arguments given, it takes 1",
                              "test.capped: o has no member 'w'",
                              "engine.node.setTransforms: argument 2: transforms.nodes holds 4000000000 elements, at most 65536",
                              "engine.node.setTransforms: argument 2: transforms.nodes holds 65537 elements, at most 65536",
                              "test.capped: o.v[0] must be number (it is array)",
                              "test.capped: o has no member 'a'",
                              "4"};
        std::string missed;
        for (std::size_t i = 0; i < 9; ++i)
            if (i >= said.size() || said[i].rfind(want[i], 0) != 0)
                missed += "\n      want " + std::string(want[i]) + " <- " + (i < said.size() ? said[i] : std::string("(no line)"));
        check(r.status == Status::Ok && said.size() == 9 && missed.empty() && reached == 1,
              "AN ARRAY OF LENGTH 4e9 (and one past its maximum, one the declaration does not take, and values nested "
              "100,000 deep) FAILS that call by name before a single element is converted or a level entered past the "
              "declaration, and the host lives on: the next call reaches its command (" + std::to_string(reached) +
                  " reached)" + missed);
    }
    // 12b. THE CALLER's BOUNDS (Host::Options::callBounds, ValueBounds): the most elements an array may hold, the deepest
    //      nesting and the most bytes - a host that states small ones FAILS each by name, and lives on (THE CONTROL)
    {
        Host::Options small;
        small.callBounds.elements = 2;
        small.callBounds.depth = 1;
        small.callBounds.bytes = 256;
        Host tight(small);
        std::vector<std::string> said;
        tight.setConsole([&said](const std::string &l) { said.push_back(l); });
        int reached = 0;
        tight.registry().add({"test.capped",
                              {Access::Reads, {object("o", {arg("v", Kind::Array, Value(Value::Array{})).upTo(4)})}, Kind::Int},
                              "how many numbers (at most 4)"},
                             [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0]["v"].asArray().size())); });
        tight.registry().add({"test.text", {Access::Reads, {object("o", {arg("s", Kind::String)})}, Kind::Int}, "its length"},
                             [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0]["s"].asString().size())); });
        const Result r = tight.run(
            "const caught = (f) => { try { f(); return 'no failure'; } catch (e) { return e.message; } };\n"
            "console.log(caught(() => test.capped({v: [1, 2, 3]})));\n"
            "console.log(caught(() => test.capped({v: [1]})));\n"
            "console.log(caught(() => test.text({s: 'x'.repeat(300)})));\n"
            "console.log(test.capped({}), test.text({s: 'x'}));\n",
            "tight.js");
        const char *want[] = {"test.capped: argument 1: o.v holds 3 elements, past the 2 one array may hold",
                              "test.capped: argument 1: o.v[0] is nested deeper than 1",
                              "test.text: argument 1: o.s would take the value past its 256 bytes",
                              "0 1"};
        std::string missed;
        for (std::size_t i = 0; i < 4; ++i)
            if (i >= said.size() || said[i].rfind(want[i], 0) != 0)
                missed += "\n      want " + std::string(want[i]) + " <- " + (i < said.size() ? said[i] : std::string("(no line)"));
        check(r.status == Status::Ok && said.size() == 4 && missed.empty() && reached == 2,
              "THE CALLER's BOUNDS: an array past its elements, a value past its depth and a string past its bytes each "
              "FAIL by name; the next calls reach their commands (" + std::to_string(reached) + " reached)" + missed);
        const Host::Options defaults;
        check(defaults.callBounds.bytes == Host::Options::kPictureBytesCap + Host::Options::kPictureBytesCap / 8 &&
                  defaults.pictureBytesCap == Host::Options::kPictureBytesCap && ValueBounds().bytes == 0,
              "THE DEFAULT CALL BOUND IS DERIVED from the picture cap (one picture at it + an eighth: " +
                  std::to_string(defaults.callBounds.bytes >> 20) + " MiB); a ValueBounds states nothing by itself");
    }
    // 12c. THE LAW AT THE DOOR (2b's final read, D1): a value whose own keys are its INDICES - every typed array but the
    //      one carried kind (a Uint8Array is bytes), a String object (over a ROPE of 2^20 units: a few nodes of the
    //      script's), an arguments object (mapped and strict) - and a DataView (else carried silently as {}) FAIL by name,
    //      "cannot be carried", BEFORE any own key is listed. The JS engine lists every index of such an object (8 bytes
    //      an index, allocated and filled: the binding's refusedClass) before a single bound could be judged: `new
    //      Int8Array(0x7fffffff)` costs the script a lazy 2 GiB and would cost the host ~16 GiB. Under a 4096-byte call
    //      bound the listing's having run shows as a failure on the BYTES at an index member ("o.55 would take ...");
    //      after, nothing is listed. THE CONTROL: a Uint8Array is still carried as bytes (the registry names its kind),
    //      and the next call reaches its command (reached = 1, its answer 2). A PROXY is refused at the same door
    //      (2b's re-read R1: its class passes every test above, and with no ownKeys trap the JS engine lists its TARGET's
    //      indices - a Proxy over a typed array - or whatever its own trap answers): over an Int8Array, over a plain
    //      object, and with an endless ownKeys trap, each FAILS "its class is Proxy" with nothing listed.
    {
        Host::Options small;
        small.callBounds.bytes = 4096;
        Host door(small);
        std::vector<std::string> said;
        door.setConsole([&said](const std::string &l) { said.push_back(l); });
        int reached = 0;
        door.registry().add({"test.capped",
                             {Access::Reads, {object("o", {arg("v", Kind::Array, Value(Value::Array{})).upTo(4)})}, Kind::Int},
                             "how many numbers (at most 4)"},
                            [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0]["v"].asArray().size())); });
        registerEngineCommands(door, headless.get(), bootError, false);
        const Result r = door.run(
            "const caught = (f) => { try { f(); return 'no failure'; } catch (e) { return e.message; } };\n"
            "const s = engine.scene.create('door'), n = engine.node.create(s);\n"
            "let rope = 'ab';\n"
            "for (let i = 0; i < 19; ++i) rope = rope + rope;\n"
            "const sloppy = (function () { return arguments; })(1, 2);\n"
            "const strict = (function () { 'use strict'; return arguments; })(1, 2);\n"
            "const keys = new Proxy({}, {get: (t, k) => k === 'length' ? 4294967295 : 'k'});\n"
            "for (const v of [new Int8Array(1 << 20), new Float64Array(1 << 20), new DataView(new ArrayBuffer(8)),\n"
            "                 new String('abc'), new String(rope), sloppy, strict, new Proxy(new Int8Array(1 << 20), {}),\n"
            "                 new Proxy({v: [1]}, {}), new Proxy({}, {ownKeys: () => keys}), new Uint8Array(4)])\n"
            "  console.log(caught(() => test.capped(v)));\n"
            "console.log(caught(() => engine.node.setTransform(s, n, new Int8Array(1 << 20))));\n"
            "console.log(rope.length, test.capped({v: [1, 2]}));\n",
            "door.js");
        const char *want[] = {"test.capped: argument 1: o cannot be carried: its class is Int8Array",
                              "test.capped: argument 1: o cannot be carried: its class is Float64Array",
                              "test.capped: argument 1: o cannot be carried: its class is DataView",
                              "test.capped: argument 1: o cannot be carried: its class is String",
                              "test.capped: argument 1: o cannot be carried: its class is String",
                              "test.capped: argument 1: o cannot be carried: its class is Arguments",
                              "test.capped: argument 1: o cannot be carried: its class is Arguments",
                              "test.capped: argument 1: o cannot be carried: its class is Proxy",
                              "test.capped: argument 1: o cannot be carried: its class is Proxy",
                              "test.capped: argument 1: o cannot be carried: its class is Proxy",
                              "test.capped: o must be object (it is bytes)",
                              "engine.node.setTransform: argument 3: transform cannot be carried: its class is Int8Array",
                              "1048576 2"};
        std::string missed;
        for (std::size_t i = 0; i < 13; ++i)
            if (i >= said.size() || said[i].rfind(want[i], 0) != 0)
                missed += "\n      want " + std::string(want[i]) + " <- " + (i < said.size() ? said[i] : std::string("(no line)"));
        check(r.status == Status::Ok && said.size() == 13 && missed.empty() && reached == 1,
              "THE LAW AT THE DOOR: a typed array but the Uint8Array, a DataView, a String object (a rope of 2^20 units "
              "too), an arguments object (mapped, strict) and a Proxy (over an Int8Array, over a plain object, with an "
              "endless ownKeys trap) FAIL by name before any own key is listed - and the "
              "brief's trigger, engine.node.setTransform(s, n, new Int8Array(...)); THE CONTROL: a Uint8Array is "
              "carried as bytes, and the next call reaches its command (" + std::to_string(reached) + " reached)" +
                  (r.status == Status::Ok ? "" : "\n      the run: " + r.message) + missed);
    }
    // 12d. THE SCRIPT's HEAP LIMIT (D1 (b), the class fix: Host::Options::memoryLimit, heapLimit()): every allocation a
    //      script drives is refused past the host's limit - an 8 MiB buffer under a 4 MiB limit FAILS its run by name
    //      ("out of memory") and the host lives on: THE CONTROL, the next run allocates 3 MiB of the 4 MiB (the refused
    //      run's heap really given back) and is served (reached = 1). And no host is unlimited: the default is DERIVED,
    //      2 x callBounds.bytes + the floor (1168 MiB). (A conversion's string refused by the limit is NOT given back:
    //      the JS engine leaks it - Script.h's named limit, homed at MCP-ON-HOST-1.)
    {
        Host::Options four;
        four.memoryLimit = std::size_t(4) << 20;
        Host heap(four);
        heap.setConsole([](const std::string &) {});
        int reached = 0;
        heap.registry().add({"test.capped",
                             {Access::Reads, {object("o", {arg("v", Kind::Array, Value(Value::Array{})).upTo(4)})}, Kind::Int},
                             "how many numbers (at most 4)"},
                            [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0]["v"].asArray().size())); });
        const Result over = heap.run("const big = new Uint8Array(8 << 20);\nhost.check({ok: big.length > 0, what: 'made'});\n",
                                     "over.js");
        const Result next = heap.run("globalThis.near = new Uint8Array(3 << 20);\n"
                                     "host.check({ok: near.length > 0 && test.capped({v: [1, 2]}) === 2, what: 'served'});\n",
                                     "next.js");
        const Host::Options defaults;
        const std::size_t derived = defaults.heapLimit();
        check(failedWith(over, "out of memory") && next.status == Status::Ok &&
                  reached == 1 && heap.checksFailed() == 0 && derived == (std::size_t(1168) << 20),
              "THE HEAP LIMIT: an 8 MiB buffer FAILS its run by name under a 4 MiB limit (an endless Proxy key list "
              "is refused at the door: 12c); THE CONTROL: the next run allocates 3 MiB and is served (" +
                  std::to_string(reached) + " reached); the default is derived, " + std::to_string(derived >> 20) +
                  " MiB\n      over: " + over.message.substr(0, 80) +
                  (next.status == Status::Ok ? std::string() : "\n      next: " + next.message));
    }
    // 12e. THE DERIVATION's x2 TERM, PROVED SMALL (the lead's ruling: no 1 GiB in a CPU row): a host that STATES small
    //      bounds - a 4 MiB picture cap and callBounds.bytes = the cap + an eighth, as the default derives them - holds
    //      a heap of 2 x its bytes + the floor (25 MiB). In it a picture AT ITS CAP made from the script's bytes AND read
    //      back (both held: the two terms) leaves the room the derivation says: one byte past that room FAILS "out of
    //      memory", and that room less 1 MiB (the interpreter's own base, measured 0.11 MiB) fits. A derivation of one
    //      x bytes (20.5 MiB) would refuse the second. THE CONTROL, under the DEFAULT limit: a full setTransforms batch
    //      (65,536 nodes, the headless engine's one node named in every element: 12.6 MiB of JS numbers) passes.
    {
        Host::Options small;
        small.pictureBytesCap = std::size_t(4) << 20;
        small.callBounds.bytes = small.pictureBytesCap + small.pictureBytesCap / 8;
        const std::size_t limit = 2 * small.callBounds.bytes + Host::Options::kHeapFloor;
        const std::size_t room = limit - 2 * small.pictureBytesCap;
        Host term(small);
        term.setConsole([](const std::string &) {});
        registerEngineCommands(term, nullptr, "no device here", false);
        const Result held = term.run(
            "globalThis.rgba = new Uint8Array(1024 * 1024 * 4);\n"
            "globalThis.p = engine.picture.create({width: 1024, height: 1024, rgba});\n"
            "globalThis.back = engine.picture.bytes(p.picture);\n"
            "host.check({ok: back.length === rgba.length, what: 'a picture at the cap, made and read back'});\n",
            "held.js");
        const Result past = term.run("globalThis.past = new Uint8Array(" + std::to_string(room + 1) + ");\n", "past.js");
        const Result fits = term.run("globalThis.fits = new Uint8Array(" + std::to_string(room - (std::size_t(1) << 20)) +
                                         ");\nhost.check({ok: fits.length > 0, what: 'the room less 1 MiB'});\n",
                                     "fits.js");
        Host full;
        full.setConsole([](const std::string &) {});
        registerEngineCommands(full, headless.get(), bootError, false);
        const Result batch = full.run(
            "const s = engine.scene.create('full-batch'), n = engine.node.create(s), N = 65536;\n"
            "engine.node.setTransforms(s, {nodes: new Array(N).fill(n), positions: new Array(3 * N).fill(0.5),\n"
            "                              rotations: new Array(4 * N).fill(0.5), scales: new Array(3 * N).fill(0.5)});\n"
            "host.check({ok: true, what: 'the full batch'});\n",
            "batch.js");
        check(term.options().heapLimit() == limit && held.status == Status::Ok && failedWith(past, "out of memory") &&
                  fits.status == Status::Ok && term.checksRun() == 2 && term.checksFailed() == 0 &&
                  batch.status == Status::Ok && full.checksFailed() == 0,
              "THE x2 TERM: a host of a 4 MiB cap derives " + std::to_string(term.options().heapLimit() >> 10) +
                  " KiB; with a picture at its cap made and read back, one byte past the room FAILS and the room less "
                  "1 MiB fits; THE CONTROL: the full 65,536-node batch passes under the default limit" +
                  (held.status == Status::Ok ? std::string() : "\n      held: " + held.message) +
                  (past.status == Status::Failed ? std::string() : "\n      past: not refused") +
                  (fits.status == Status::Ok ? std::string() : "\n      fits: " + fits.message) +
                  (batch.status == Status::Ok ? std::string() : "\n      batch: " + batch.message));
    }

    // 12f. THE SMALL LAWS (2b's final read L-d, L-f, L-g, L-h), each FAILS by name where it went through silently:
    //      a member name holding a NUL is refused (it was cut at the NUL - 'v\0x' read as the declared 'v'); a
    //      DETACHED ArrayBuffer is refused with the JS engine's own error taken (it was carried as empty bytes, the
    //      error left pending); a kind MINTED by Registry::name is a named kind from then on, so a later binding of it
    //      to a C++ type is a registry FAULT (it bound silently); an enum value its name table lacks FAILS by name (it
    //      went out as an integer under a declared string). THE CONTROL: the next call reaches its command (reached
    //      1), a fresh kind still binds, and a named enum value goes out as its name.
    {
        Host small;
        std::vector<std::string> said;
        small.setConsole([&said](const std::string &l) { said.push_back(l); });
        int reached = 0;
        small.registry().add({"test.capped",
                              {Access::Reads, {object("o", {arg("v", Kind::Array, Value(Value::Array{})).upTo(4)})}, Kind::Int},
                              "how many numbers (at most 4)"},
                             [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0]["v"].asArray().size())); });
        small.registry().add({"test.size", {Access::Reads, {arg("b", Kind::Bytes)}, Kind::Int}, "how many bytes"},
                             [&reached](Call &c) { ++reached; return Value(std::int64_t(c[0].asBytes().size())); });
        const Result r = small.run(
            "const caught = (f) => { try { f(); return 'no failure'; } catch (e) { return e.message; } };\n"
            "console.log(caught(() => test.capped({'v\\0x': [1]})));\n"
            "const gone = new ArrayBuffer(8);\n"
            "gone.transfer();\n"
            "console.log(caught(() => test.size(gone)));\n"
            "console.log(test.size(new Uint8Array(3)));\n",
            "small.js");
        const char *want[] = {"test.capped: argument 1: o has a member name holding a NUL character",
                              "test.size: argument 1: b: TypeError: ",
                              "3"};
        std::string missed;
        for (std::size_t i = 0; i < 3; ++i)
            if (i >= said.size() || said[i].rfind(want[i], 0) != 0)
                missed += "\n      want " + std::string(want[i]) + " <- " + (i < said.size() ? said[i] : std::string("(no line)"));
        Registry minted;
        const bool mintedOk = minted.name("gadget", 7) != 0;
        minted.bindKind<int>("gadget");
        minted.bindKind<int>("fresh");
        const std::vector<std::string> faults = minted.validate();
        std::string unnamed = "no failure";
        try {
            toValue(tr::Partial::Unnamed);
        } catch (const std::exception &e) {
            unnamed = e.what();
        }
        check(r.status == Status::Ok && missed.empty() && reached == 1 && mintedOk && faults.size() == 1 &&
                  faults[0].rfind("gadget: ", 0) == 0 && unnamed.find("has no name in its table") != std::string::npos &&
                  toValue(tr::Partial::AlsoNamed) == Value("alsoNamed"),
              "THE SMALL LAWS: a NUL in a member name, a detached ArrayBuffer, a minted kind bound later (" +
                  std::to_string(faults.size()) + " fault(s)) and an unnamed enum value (" + unnamed + ") each FAIL by "
                  "name; THE CONTROL: the next call reaches its command (" + std::to_string(reached) + " reached), a "
                  "fresh kind binds, a named value goes out as its name" + missed);
    }

    // 13. ONE FIELD TABLE PER RECORD (2b b1; records.h): a record's fields named once - the member pointer DERIVES the
    //     parameter (the record's default as each member's), both conversions, and the guard. On this file's records:
    //     the table is the record's fields in order (the structured binding's offsets); the record's default and a
    //     record with EVERY field changed each go C++ -> value -> the registry's check -> C++ and come back equal; a
    //     float past its range and a flat vector of a broken length FAIL by name. THE CONTROL: a table with two entries
    //     swapped is found out of order (by name)
    {
        std::string why, swappedWhy;
        const bool ordered = Record<tr::Inner>::table().inOrder(why) && Record<tr::Outer>::table().inOrder(why);
        const Table<tr::Inner> swapped({field("at", &tr::Inner::at), field("level", &tr::Inner::level)});
        const bool caught = !swapped.inOrder(swappedWhy) &&
                            swappedWhy == "entry 0 'at' is not the record's field 0 (its offset 4, the field's 0)";
        Registry rec;
        tr::Outer seen;
        const bool added = rec.add({"test.record", {Access::Changes, {Record<tr::Outer>::table().param("r")}, Kind::Object},
                                    "answers the record it was given, through C++"},
                                   [&seen](Call &c) -> Value {
                                       std::string w;
                                       seen = tr::Outer();
                                       if (!Record<tr::Outer>::table().fromValue(c[0], seen, "r", w)) return c.fail(w);
                                       return Record<tr::Outer>::table().toValue(seen);
                                   });
        tr::Outer filled;
        filled.on = false;
        filled.mode = tr::Mode::Slow;
        filled.count = 4000000000u;
        filled.gain = -2.25f;
        filled.exact = 1.0 / 3.0;
        filled.label = "utf-8 \xc3\xa9";
        filled.turn = Quat(0.5f, 0.5f, 0.5f, 0.5f);
        filled.tint = Colour(1, 0.5f, 0.25f, 0.75f);
        filled.inner.level = 9;
        filled.inner.at = Vec3(-1, -2, -3);
        filled.list = {tr::Inner{3, Vec3(4, 5, 6)}, tr::Inner{0, Vec3()}};   // (a list: elements, not flat)
        filled.points = {Vec3(1, 1, 1), Vec3(2, 2, 2)};
        filled.turns = {Quat(0, 1, 0, 0)};
        filled.tints = {Colour(1, 1, 1, 0.5f), Colour(0, 0, 0, 1)};
        filled.sh[0] = 7;
        filled.sh[2] = -7;
        filled.maybe = -5;
        filled.shared = std::make_shared<const tr::Inner>(tr::Inner{1, Vec3(9, 8, 7)});
        const Value defaults = Record<tr::Outer>::table().toValue(tr::Outer());
        const Value whole = Record<tr::Outer>::table().toValue(filled);
        const Result d1 = rec.call("test.record", {}), d2 = rec.call("test.record", {Value(Value::Object{})});
        const Result w1 = rec.call("test.record", {whole});
        const bool backEqual = w1.status == Status::Ok && w1.value == whole && seen.count == 4000000000u &&
                               seen.shared && seen.shared->at == Vec3(9, 8, 7) && seen.maybe == -5 && seen.sh[2] == -7.0f &&
                               seen.points.size() == 2 && seen.list[0].at == Vec3(4, 5, 6) && seen.mode == tr::Mode::Slow &&
                               seen.turns.size() == 1 && seen.tints[1] == Colour(0, 0, 0, 1);
        const auto with = [&](const char *member, Value v) {
            return rec.call("test.record", {Value(Value::Object{{member, std::move(v)}})});
        };
        check(ordered && caught && added && rec.validate().empty(),
              "THE GUARD: each table is its record's fields in declaration order (" + why + "); THE CONTROL: two entries "
              "swapped are caught by name (" + swappedWhy + "); the record's command registers clean");
        check(d1.status == Status::Ok && d1.value == defaults && d2.value == defaults && backEqual,
              "a record's DEFAULT (absent, and {}) and a record with EVERY field changed (an enum, 4e9 unsigned, a float, "
              "1/3, UTF-8, a Quat, a Colour, a nested record, a vector of records, a flat Vec3 vector, a float[3], an "
              "optional, a shared_ptr<const T>) go C++ -> value -> the registry -> C++ and come back equal");
        const Result past = with("gain", Value(1e39));
        check(failedWith(past, "test.record: r.gain ") && past.message.find(" is beyond a float") != std::string::npos &&
                  failedWith(with("points", Value(Value::Array{Value(1), Value(2)})),
                             "test.record: r.points holds 2 numbers, not a multiple of 3") &&
                  failedWith(with("turns", Value(Value::Array(5, Value(0)))), "r.turns holds 5 numbers, not a multiple of 4") &&
                  failedWith(with("tints", Value(Value::Array(6, Value(0)))), "r.tints holds 6 numbers, not a multiple of 4") &&
                  failedWith(with("points", Value(Value::Array{Value(1e39), Value(0), Value(0)})), "r.points[0] ") &&
                  with("points", Value(Value::Array{Value(1e39), Value(0), Value(0)})).message.find(" is beyond a float") !=
                      std::string::npos &&
                  failedWith(with("mode", Value("medium")), "r.mode 'medium' is not one of \"off\"|\"fast\"|\"slow\"") &&
                  failedWith(with("count", Value(-1)), "r.count -1 is outside 0..4294967295"),
              "a float past its range (a field and a flat vector's element), a flat vector of a broken length (each stride: "
              "3, 4, 4), an unnamed enum value and a negative unsigned each FAIL by name");
        const std::string sig = signatureText(*rec.find("test.record"));
        check(sig.find("gain: float = default") != std::string::npos && sig.find("exact: number = default") != std::string::npos &&
                  sig.find("points: float[3n] <= 12 numbers = default") != std::string::npos &&
                  sig.find("turn: float[4] = default") != std::string::npos,
              "THE SIGNATURE SAYS single precision (`float`, a double is `number`) and a flat vector's bound in numbers: " +
                  sig.substr(0, 200));
    }

    // 14. DESTROY AND THE OWNERSHIP LAW (2b b2; the lead's ruling for the lab's fresh host per Run), on the headless
    //     engine. DESTROY (L7): a destroyed node, mesh and material, and another scene's node, each FAIL by name at the
    //     next use (the engine is asked); a destroyed scene's handle is STALE; a scene the host's owner handed in is not
    //     the script's to destroy. THE OWNERSHIP: a host that made a scene of its own, and a mesh, a material and a node
    //     drawing them in a scene its owner HANDED IN, ends - the handed-in scene survives, everything the run made is
    //     gone (the presence reads; the made scene's name is free again); THE CONTROL: all of it present while the host
    //     lived. (The views' half needs a device: the card batch.)
    {
        Host d;
        d.setConsole([](const std::string &) {});
        registerEngineCommands(d, headless.get(), bootError, false);
        Registry &dr = d.registry();
        const auto made = [&](const char *name, Value::Array args) { return dr.call(name, std::move(args)).value; };
        const Value sa = made("engine.scene.create", {Value("destroy-a")}), sb = made("engine.scene.create", {Value("destroy-b")});
        const Value na = made("engine.node.create", {sa}), nb = made("engine.node.create", {sb});
        const Value mesh = made("engine.mesh.unitCube", {sa}), mat = made("engine.material.createPbr", {sa, Value(Value::Object{})});
        const auto draw = [&](const Value &node) {
            return dr.call("engine.node.attachMesh", {sa, node, Value(Value::Object{{"mesh", mesh}, {"material", mat}})});
        };
        const bool live = draw(na).status == Status::Ok;
        const bool gone = dr.call("engine.node.destroy", {sa, na}).value == Value(true) &&
                          failedWith(dr.call("engine.node.setTransform", {sa, na, Value(Value::Object{})}),
                                     "node: node " + std::to_string(Call::idOf(na)) + " is not a node of scene 'destroy-a'") &&
                          failedWith(dr.call("engine.node.setTransform", {sa, nb, Value(Value::Object{})}),
                                     "is not a node of scene 'destroy-a'") &&
                          dr.call("engine.mesh.destroy", {sa, mesh}).value == Value(true) &&
                          dr.call("engine.material.destroy", {sa, mat}).value == Value(true) &&
                          failedWith(draw(made("engine.node.create", {sa})), "mesh: mesh " + std::to_string(Call::idOf(mesh)) +
                                                                              " is not a mesh of scene 'destroy-a'");
        const std::int64_t sbHandle = sb.asInt();
        const bool stale = dr.call("engine.scene.destroy", {sb}).value == Value(true) &&
                           failedWith(dr.call("engine.node.create", {sb}), "is stale (its scene was released)") &&
                           failedWith(dr.call("engine.node.setTransform", {sb, nb, Value(Value::Object{})}), "is stale") &&
                           sbHandle == sb.asInt();
        Scene *handed = headless->createScene("handed-in");
        const Value h(dr.hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
        check(live && gone && stale &&
                  failedWith(dr.call("engine.scene.destroy", {h}), "the scene 'handed-in' was handed in by the host's owner"),
              "DESTROY: a destroyed node, mesh and material and another scene's node FAIL by name at the next use; a "
              "destroyed scene's handle is stale; a handed-in scene is not the script's to destroy; THE CONTROL: the "
              "node drew its mesh while they lived");
        headless->destroyScene(handed);
    }
    {
        Scene *handed = headless->createScene("handed-in");
        NodeId node = 0;
        MeshId mesh = 0;
        MaterialId mat = 0;
        bool whileLived = false;
        {
            Host run;
            run.setConsole([](const std::string &) {});
            registerEngineCommands(run, headless.get(), bootError, false);
            const Value h(run.registry().hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
            const Result r = run.run("const s = " + std::to_string(h.asInt()) + ";\n"
                                     "const mesh = engine.mesh.unitCube(s), material = engine.material.createPbr(s, {});\n"
                                     "globalThis.node = engine.node.create(s);\n"
                                     "engine.node.attachMesh(s, node, {mesh, material});\n"
                                     "globalThis.ids = [mesh, material];\n"
                                     "const own = engine.scene.create('made-by-run');\n"
                                     "engine.node.attachMesh(own, engine.node.create(own), {mesh: engine.mesh.unitCube(own),\n"
                                     "                       material: engine.material.createPbr(own, {})});\n"
                                     "host.check({ok: true, what: 'made'});\n",
                                     "ownership.js");
            // the made ids, read back through the script's own globals
            std::vector<std::string> said;
            run.setConsole([&said](const std::string &l) { said.push_back(l); });
            run.run("console.log(node, ids[0], ids[1]);\n", "ids.js");
            if (r.status == Status::Ok && said.size() == 1) {
                unsigned long long n = 0, m = 0, t = 0;
                std::sscanf(said[0].c_str(), "%llu %llu %llu", &n, &m, &t);
                node = NodeId(n & 0xffffffffull);
                mesh = MeshId(m & 0xffffffffull);
                mat = MaterialId(t & 0xffffffffull);
            }
            whileLived = node && mesh && mat && handed->nodePresence(node) == Scene::Presence::Present &&
                         handed->meshPresence(mesh) == Scene::Presence::Present &&
                         handed->materialPresence(mat) == Scene::Presence::Present &&
                         headless->createScene("made-by-run") == nullptr;   // (its name is taken: the run's scene lives)
        }   // THE HOST ENDS
        Scene *again = headless->createScene("made-by-run");
        check(whileLived && handed->nodePresence(node) == Scene::Presence::Absent &&
                  handed->meshPresence(mesh) == Scene::Presence::Absent &&
                  handed->materialPresence(mat) == Scene::Presence::Absent && again != nullptr &&
                  headless->createScene("handed-in") == nullptr,   // (the handed-in scene's name still taken: it lives)
              "THE OWNERSHIP LAW: a run's node, mesh and material in a HANDED-IN scene are gone at its host's end (the "
              "node first: it drew them), the scene the run made is destroyed (its name free again), the handed-in scene "
              "survives; THE CONTROL: all of it present while the host lived");
        if (again) headless->destroyScene(again);
        headless->destroyScene(handed);
    }
    // 14c. A HANDED-IN SCENE's OWN OBJECTS ARE READ-ONLY TO A SCRIPT (2b's final read L-a; the lead's ruling (ii)): a
    //      handle is computable (owner | kind | id), so a script can name its owner's node, mesh and material. A command
    //      CHANGES or DESTROYS only what its own host made there (the per-scene made record is the authority): the
    //      owner's node moved (setTransform, setTransforms), re-drawn (attachMesh) or removed, its mesh and material
    //      removed, and the scene's own settings (setAmbient, gi.set) each FAIL by name, and the owner's objects are all
    //      present and unmoved after. THE CONTROL: the script's OWN node in the same scene moves, draws and is
    //      destroyed. (A handed-in VIEW is the script's to drive, not to destroy: script.view_ownership, on the card.)
    {
        Scene *handed = headless->createScene("owner-world");
        const NodeId ownerNode = handed->createNode();
        const MeshId ownerMesh = handed->createMesh(enginetest::unitCubeMesh());
        const MaterialId ownerMat = handed->createPbrMaterial(PbrParams{});
        handed->setNodeTransform(ownerNode, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
        NodeId mineId = 0;
        bool moved = false;
        Result r;
        std::vector<std::string> said;
        {
            Host guest;
            guest.setConsole([&said](const std::string &l) { said.push_back(l); });
            registerEngineCommands(guest, headless.get(), bootError, false);
            const Value h(guest.registry().hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
            r = guest.run(
                "const s = " + std::to_string(h.asInt()) + ";\n"
                "const caught = (f) => { try { f(); return 'no failure'; } catch (e) { return e.message; } };\n"
                "const forge = (own, id) => Math.floor(own / 2 ** 32) * 2 ** 32 + id;\n"
                "const node = engine.node.create(s), gone = engine.node.create(s);\n"
                "const mesh = engine.mesh.unitCube(s), material = engine.material.createPbr(s, {});\n"
                "const theirs = forge(node, " + std::to_string(ownerNode) + ");\n"
                "console.log(caught(() => engine.node.setTransform(s, theirs, {position: [5, 5, 5]})));\n"
                "engine.node.setTransform(s, node, {position: [1, 2, 3]});\n"
                "console.log(caught(() => engine.node.setTransforms(s, {nodes: [node, theirs], positions: [7, 8, 9, 5, 5, 5]})));\n"
                "console.log(caught(() => engine.node.attachMesh(s, theirs, {mesh, material})));\n"
                "console.log(caught(() => engine.node.destroy(s, theirs)));\n"
                "console.log(caught(() => engine.mesh.destroy(s, forge(mesh, " + std::to_string(ownerMesh) + "))));\n"
                "console.log(caught(() => engine.material.destroy(s, forge(material, " + std::to_string(ownerMat) + "))));\n"
                "console.log(caught(() => engine.scene.setAmbient(s, {upper: [1, 1, 1], lower: [0, 0, 0]})));\n"
                "console.log(caught(() => engine.gi.set(s, {mode: 'off', cascades: []})));\n"
                "engine.node.attachMesh(s, node, {mesh, material});\n"
                "console.log(engine.node.destroy(s, gone), node % 2 ** 32);\n",
                "readonly.js");
            if (said.size() == 9) mineId = NodeId(std::stoull(said[8].substr(said[8].find(' ') + 1)));
            Vec3 p;
            Quat q;
            moved = mineId && handed->nodeWorldPose(mineId, p, q) && p.x == 1.0f && p.y == 2.0f && p.z == 3.0f;
        }
        const std::string owner = "the scene 'owner-world' was handed in by the host's owner";
        const std::string want[] = {"node " + std::to_string(ownerNode) + " is not this host's to change: " + owner,
                                    "node " + std::to_string(ownerNode) + " is not this host's to change: " + owner,
                                    "node " + std::to_string(ownerNode) + " is not this host's to change: " + owner,
                                    "node " + std::to_string(ownerNode) + " is not this host's to destroy: " + owner,
                                    "mesh " + std::to_string(ownerMesh) + " is not this host's to destroy: " + owner,
                                    "material " + std::to_string(ownerMat) + " is not this host's to destroy: " + owner,
                                    owner + ": its settings are not this host's to change",
                                    owner + ": its settings are not this host's to change",
                                    "true "};
        std::string missed;
        for (std::size_t i = 0; i < 9; ++i)
            if (i >= said.size() || said[i].find(want[i]) == std::string::npos)
                missed += "\n      want " + want[i] + " <- " + (i < said.size() ? said[i] : std::string("(no line)"));
        Vec3 at;
        Quat turn;
        const bool unmoved = handed->nodeWorldPose(ownerNode, at, turn) && at.x == 0.0f && at.y == 0.0f && at.z == 0.0f;
        check(r.status == Status::Ok && missed.empty() && moved && unmoved &&
                  handed->nodePresence(ownerNode) == Scene::Presence::Present &&
                  handed->meshPresence(ownerMesh) == Scene::Presence::Present &&
                  handed->materialPresence(ownerMat) == Scene::Presence::Present,
              "A HANDED-IN SCENE's OWN OBJECTS ARE READ-ONLY: the owner's node moved (one and in a batch), re-drawn and "
              "removed, its mesh and material removed, and the scene's settings each FAIL by name; the owner's objects "
              "present and unmoved after; the refused batch moved NOTHING (the script's own node, its first element, "
              "still at the (1, 2, 3) it was set to, not the batch's (7, 8, 9)); THE CONTROL: the script's own node "
              "moved, drew and was destroyed" +
                  (r.status == Status::Ok ? std::string() : "\n      the run: " + r.message) + missed);
        headless->destroyScene(handed);
    }

    // 15. THE FRAME's PUBLIC DOOR AND ONE CLOCK WRITER A SCENE (2b b3; ruling 6, the lead's ruling on the stamp): a frame
    //     steps each scene THIS HOST MADE through its FrameInput part (one submitFrame a frame, stepSerial +1); a scene
    //     its owner HANDED IN is never stamped (its owner's mirror is its one writer) - a host holding only that makes
    //     no submit at all. THE CONTROL: the made scene's serial advances by one a frame.
    {
        Scene *handed = headless->createScene("clock-handed");
        unsigned long long handedOnly = 0, withMade = 0, madeSerial = 0;
        {
            Host clock;
            clock.setConsole([](const std::string &) {});
            registerEngineCommands(clock, headless.get(), bootError, false);
            Registry &cr = clock.registry();
            const Value h(cr.hold("scene", std::shared_ptr<Scene>(handed, [](Scene *) {})));
            const unsigned long long s0 = headless->frameSubmitStatus().submitted;
            cr.call("engine.frames", {Value(3)});
            handedOnly = headless->frameSubmitStatus().submitted - s0;
            const Value made = cr.call("engine.scene.create", {Value("clock-made")}).value;
            const unsigned long long s1 = headless->frameSubmitStatus().submitted;
            cr.call("engine.frames", {Value(3)});
            withMade = headless->frameSubmitStatus().submitted - s1;
            madeSerial = cr.object<Scene>(made.asInt())->published().stepSerial;
        }
        check(handedOnly == 0 && withMade == 3 && madeSerial == 3,
              "ONE CLOCK WRITER A SCENE: 3 frames over a handed-in scene alone submit nothing (" +
                  std::to_string(handedOnly) + "); THE CONTROL: with a scene the host made, one submit a frame (" +
                  std::to_string(withMade) + ") and its serial is 3 (" + std::to_string(madeSerial) + ")");
        headless->destroyScene(handed);
    }

    if (gFail) std::printf("script.host: FAILED (%d)\n", gFail);
    else std::printf("script.host: PASSED\n");
    return gFail ? 1 : 0;
}
