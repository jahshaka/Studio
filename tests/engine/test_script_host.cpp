// script.host — THE ENGINE'S SCRIPT HOST, ITS LAWS ON THE CPU (SCRIPT-IN-ENGINE-1 stage 1; jahshaka/engine/Script.h).
// No device, no engine: a registry of this file's own commands.
// THE LAWS: a value of every kind is itself after a copy and compares by content (an Int equals a Number of the same
// number); every command carries a name and a doc (validate is empty); THE STRICT CALL — an unregistered name, a surplus
// argument, an undeclared object member, a missing argument with no default and a non-integral number for an integer
// each FAIL by name; null and absent take the declared default; an integral number is an integer; a fail is the
// caller's mistake (Failed, null), a refuse the engine's state (Refused, the declared return's falsy value, the message
// kept as lastError).
#include "jahshaka/engine/Script.h"
#include "script/RouteDeclaration.h"   // the route declaration's judgement (engine-private, pure)

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

using namespace jahshaka::engine;
using namespace jahshaka::engine::script;

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
        reg.add({"test.add", {{target("a", Kind::Int), object("more", {arg("b", Kind::Int, Value(10))}, Value(Value::Object{}))},
                             Kind::Int}, "a + more.b"},
                [](Call &c) { return Value(c[0].asInt() + c[1]["b"].asInt()); }) &&
        reg.add({"test.opts", {{object("opts", {arg("n", Kind::Number, Value(1.0)), numbers("v", 3)})}, Kind::Number},
                 "opts.n + the sum of opts.v"},
                [](Call &c) {
                    double s = c[0]["n"].asNumber();
                    for (const Value &x : c[0]["v"].asArray()) s += x.asNumber();
                    return Value(s);
                }) &&
        reg.add({"test.mistake", {{}, Kind::Bool}, "fails: the caller's mistake"},
                [](Call &c) { return c.fail("you asked for a mistake"); }) &&
        reg.add({"test.notNow", {{}, Kind::Bool}, "refuses: the engine's state"},
                [](Call &c) { return c.refuse("not now"); });
    check(added && reg.validate().empty(),
          "four commands registered; validate is empty: every command has a name, a signature and a doc, and its shape");
    Registry twice;
    const bool first = twice.add({"test.one", {{}, Kind::Null}, "once"}, [](Call &) { return Value(); });
    const bool second = twice.add({"test.one", {{}, Kind::Null}, "again"}, [](Call &) { return Value(); });
    check(first && !second && twice.validate().size() == 1 &&
              twice.validate()[0].find("test.one: registered twice") == 0 && twice.commands().size() == 1,
          "a second command of one name is refused AND kept as a fault (" +
              (twice.validate().empty() ? std::string("none") : twice.validate()[0]) + ")");
    Registry undocumented;
    undocumented.add({"test.bare", {{arg("", Kind::Int)}, Kind::Null}, ""}, [](Call &) { return Value(); });
    check(undocumented.validate().size() == 2, "validate names a command with no doc and a parameter with no name");
    Registry misshapen;
    misshapen.add({"test.pair", {{arg("a", Kind::Int), arg("b", Kind::Int)}, Kind::Null}, "two positional numbers"},
                  [](Call &) { return Value(); });
    misshapen.add({"test.late", {{arg("opts", Kind::Bool), target("scene", Kind::Int)}, Kind::Null}, "a target last"},
                  [](Call &) { return Value(); });
    const std::vector<std::string> faults = misshapen.validate();
    check(faults.size() == 2 && faults[0].find("test.pair: 2 positional parameters are not targets") == 0 &&
              faults[1].find("test.late: the target 'scene' follows a named parameter") == 0,
          "THE SHAPE RULE: validate names two non-target positional parameters and a target after a named one");

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
    check(signatureText(*reg.find("test.add")) == "test.add(a: target int, more: object {b: int = default} = default) -> int",
          "the signature reads as text: " + signatureText(*reg.find("test.add")));

    // 4. THE HOST AND ITS SCRIPT LANGUAGE (JavaScript): the same registry's commands as functions of their roots
    Host host;
    std::vector<std::string> lines;
    host.setConsole([&lines](const std::string &l) { lines.push_back(l); });
    Registry &hr = host.registry();
    hr.add({"test.add", {{target("a", Kind::Int), object("more", {arg("b", Kind::Int, Value(10))}, Value(Value::Object{}))},
                        Kind::Int}, "a + more.b"},
           [](Call &c) { return Value(c[0].asInt() + c[1]["b"].asInt()); });
    hr.add({"test.opts", {{object("opts", {arg("n", Kind::Number, Value(1.0)), numbers("v", 3)})}, Kind::Number},
            "opts.n + the sum of opts.v"},
           [](Call &c) {
               double s = c[0]["n"].asNumber();
               for (const Value &x : c[0]["v"].asArray()) s += x.asNumber();
               return Value(s);
           });
    hr.add({"test.mistake", {{}, Kind::Bool}, "fails: the caller's mistake"},
           [](Call &c) { return c.fail("you asked for a mistake"); });
    hr.add({"test.notNow", {{}, Kind::Bool}, "refuses: the engine's state"},
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
    hr.add({"test.make", {{}, Kind::Object}, "one value of every kind"}, [&made](Call &) { return made; });
    hr.add({"test.keep", {{keepParam}, Kind::Null}, "stores its argument"},
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
    check(engineCommands == 20 && eng.validate().empty(),
          "the 20 engine commands register, each documented and shaped (targets positional, the rest named), with no engine");
    const Value fakeScene(eng.hold("scene", std::make_shared<int>(0))), fakeNode(eng.name("node", 1));
    check(failedWith(eng.call("engine.node.setTransform",
                              {fakeScene, fakeNode, Value(Value::Object{{"position", Value(Value::Array{Value(0), Value(1)})}})}),
                     "engine.node.setTransform: transform.position must hold 3 elements"),
          "a 2-element position FAILS by name (the host checks a vector's length, before the engine is asked)");
    check(failedWith(eng.call("engine.scene.create", {Value("s")}), "engine.scene.create: the engine did not start: no device here") &&
              failedWith(eng.call("engine.frames", {Value(1)}), "the engine did not start: no device here"),
          "with no engine every engine command FAILS saying why the engine is absent");
    Value::Bytes px(4 * 2 * 4);
    for (std::size_t i = 0; i < px.size(); ++i) px[i] = std::uint8_t(i * 7);   // byte i = 7 i: a column steps 28 a channel
    const Value pic(Value::Object{{"width", Value(4)}, {"height", Value(2)}, {"rgba", Value::bytes(px)}});
    unsigned long long h = 14695981039346656037ull;
    for (std::uint8_t b : px) { h ^= b; h *= 1099511628211ull; }
    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx", h);
    check(eng.call("engine.view.hash", {pic}).value == Value(hex), std::string("engine.view.hash is the FNV-1a 64 of the bytes (") + hex + ")");
    const auto span = [](const char *axis, int a, int b, int from, int to) {
        return Value(Value::Object{{"axis", Value(axis)}, {"a", Value(a)}, {"b", Value(b)}, {"from", Value(from)}, {"to", Value(to)}});
    };
    // column 3 against column 2: every byte 4 apart = 28 codes; row 1 against row 0: 16 bytes apart = 112 codes
    check(eng.call("engine.view.columnStep", {pic, span("column", 3, 2, 0, 2)}).value == Value(28.0) &&
              eng.call("engine.view.columnStep", {pic, span("row", 1, 0, 1, 3)}).value == Value(112.0),
          "engine.view.columnStep: a column step of 4 bytes = 28 codes, a row step of 16 bytes = 112 codes");
    const Value rect(Value::Object{{"x0", Value(1)}, {"y0", Value(0)}, {"x1", Value(3)}, {"y1", Value(2)}});
    // pixels (1,0) (2,0) (1,1) (2,1) = byte bases 4, 8, 20, 24: the red mean 7 * 14 = 98, each channel one step up
    check(eng.call("engine.view.mean", {pic, rect}).value ==
              Value(Value::Array{Value(98.0), Value(105.0), Value(112.0), Value(119.0)}),
          "engine.view.mean over [1,3) x [0,2) = [98, 105, 112, 119]");
    check(failedWith(eng.call("engine.view.columnStep", {pic, span("column", 4, 2, 0, 2)}), "outside the picture") &&
              failedWith(eng.call("engine.view.mean", {Value(Value::Object{{"width", Value(4)}, {"height", Value(3)},
                                                                            {"rgba", Value::bytes(px)}}), rect}),
                         "not width x height x 4"),
          "a span outside the picture and a picture whose bytes are not width x height x 4 FAIL");

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
        const bool refused = !broken.registry().add({"test.bare", {{}, Kind::Null}, ""}, [](Call &) { return Value(); });
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
        thrower.registry().add({"test.throws", {{}, Kind::Null}, "throws a std::exception"},
                               [](Call &) -> Value { throw std::runtime_error("boom"); });
        thrower.registry().add({"test.throwsOdd", {{}, Kind::Null}, "throws an int"}, [](Call &) -> Value { throw 42; });
        const Result direct = thrower.registry().call("test.throws", {});
        const Result odd = thrower.registry().call("test.throwsOdd", {});
        const Result js = thrower.run("try { test.throws(); } catch (e) { console.log(e.message); }\ntest.throwsOdd();\n",
                                      "throws.js");
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
            r.add({"test.make", {{arg("kind", Kind::String)}, Kind::Int}, "a held int of that kind -> handle"},
                  [](Call &c) { return c.hold(c[0].asString(), std::make_shared<int>(7)); });
        };
        make(a);
        make(other);
        a.add({"test.use", {{target(handle("scene", "scene"))}, Kind::Int}, "the held int"},
              [](Call &c) { return Value(*c.object<int>(c[0])); });
        a.add({"test.free", {{target(handle("scene", "scene"))}, Kind::Bool}, "releases it"},
              [](Call &c) { return Value(c.registry().release(c[0].asInt())); });
        a.add({"test.named", {{target(handle("scene", "scene")), object("o", {handle("node", "node")})}, Kind::Int},
               "the node's engine id"},
              [](Call &c) { return Value(std::int64_t(Call::idOf(c[1]["node"]))); });
        a.add({"test.mint", {{arg("id", Kind::Int)}, Kind::Int, "node"}, "a node handle of that id"},
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
        check(failedWith(a.call("test.mint", {Value(std::int64_t(1) << 32 | 5)}), "does not fit a handle's 32 bits"),
              "an engine id over 2^32 - 1 is refused at minting (2^32 + 5 never becomes 5)");
        check(signatureText(*a.find("test.named")) == "test.named(scene: target scene handle, o: object {node: node handle}) -> int" &&
                  signatureText(*a.find("test.mint")) == "test.mint(id: int) -> node handle",
              "the signature prints the handles' kinds: " + signatureText(*a.find("test.named")));
        Host jsHost;
        jsHost.setConsole([](const std::string &) {});
        make(jsHost.registry());
        jsHost.registry().add({"test.use", {{target(handle("scene", "scene"))}, Kind::Int}, "the held int"},
                              [](Call &c) { return Value(*c.object<int>(c[0])); });
        const Result js = jsHost.run("const s = test.make('scene'), v = test.make('view');\n"
                                     "host.check({ok: test.use(s) === 7, what: 'a scene handle through JS'});\n"
                                     "test.use(v);\n", "handles.js");
        check(js.status == Status::Failed && jsHost.checksFailed() == 0 && js.message.find("handles.js:3") != std::string::npos &&
                  js.message.find("is a view handle, not a scene handle") != std::string::npos,
              "through JS a handle is a number that keeps its kind: the wrong kind FAILS with the script's line");
        // the engine's own commands, no engine: a view handle bound as the scene FAILS before the command runs
        const Value v(eng.hold("view", std::make_shared<int>(0)));
        check(failedWith(eng.call("engine.view.setScene", {v, Value(Value::Object{{"scene", v}})}),
                         "bind.scene: " + std::to_string(v.asInt()) + " is a view handle, not a scene handle"),
              "engine.view.setScene(view, {scene: view}) FAILS by name (it bound the wrong object silently before)");
    }

    if (gFail) std::printf("script.host: FAILED (%d)\n", gFail);
    else std::printf("script.host: PASSED\n");
    return gFail ? 1 : 0;
}
