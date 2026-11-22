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
#include <limits>
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
    hr.add({"test.keep", {Access::Reads, {keepParam}, Kind::Null}, "stores its argument"},
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
    check(engineCommands == 23 && eng.validate().empty(),
          "the 23 engine commands register, each documented and shaped (targets positional, the rest named), with no engine");
    {
        // THE 25's ACCESS (the spec's table, 2a a1): these read; every other command changes the engine's state
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
        check(checked == 25 && wrong.empty(),
              "every one of the " + std::to_string(checked) + " commands declares the access of the spec's table: 8 " +
                  "reads, the rest changes" + (wrong.empty() ? std::string() : " - wrong:" + wrong));
        check(signatureText(*eng.find("engine.picture.hash")) == "engine.picture.hash(picture: target picture handle) -> string [reads]",
              "the signature prints its access last: " + signatureText(*eng.find("engine.picture.hash")));
    }
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
            r.add({"test.make", {Access::Reads, {arg("kind", Kind::String)}, Kind::Int}, "a held int of that kind -> handle"},
                  [](Call &c) { return c.hold(c[0].asString(), std::make_shared<int>(7)); });
        };
        make(a);
        make(other);
        a.add({"test.use", {Access::Reads, {target(handle("scene", "scene"))}, Kind::Int}, "the held int"},
              [](Call &c) { return Value(*c.object<int>(c[0])); });
        a.add({"test.free", {Access::Reads, {target(handle("scene", "scene"))}, Kind::Bool}, "releases it"},
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
        a.add({"test.many", {Access::Reads, {target(handle("scene", "scene")), object("o", {handles("nodes", "node")})},
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
            counted.add({"test.many", {Access::Reads, {target(handle("scene", "scene")), object("o", {handles("nodes", "node")})},
                                       Kind::Int}, "how many nodes"},
                        [](Call &c) { return Value(std::int64_t(c[1]["nodes"].asArray().size())); });
            counted.checkKind("node", [&asked](const Call &, std::uint32_t, std::string &) { ++asked; return true; });
            const Value s(counted.hold("scene", std::make_shared<int>(0)));
            Value::Array seven;
            for (int i = 1; i <= 7; ++i) seven.push_back(Value(counted.name("node", std::uint32_t(i))));
            const Result r7 = counted.call("test.many", {s, Value(Value::Object{{"nodes", Value(seven)}})});
            check(r7.value == Value(7) && asked == 7 &&
                      signatureText(*counted.find("test.many")) ==
                          "test.many(scene: target scene handle, o: object {nodes: node handle[]}) -> int [reads]",
                  "the existence check is asked once for EACH of 7 elements (" + std::to_string(asked) +
                      " asked); the signature prints node handle[]");
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
        // the engine's own commands, no engine: a view handle bound as the scene FAILS before the command runs
        const Value v(eng.hold("view", std::make_shared<int>(0)));
        check(failedWith(eng.call("engine.view.setScene", {v, Value(Value::Object{{"scene", v}})}),
                         "bind.scene: " + std::to_string(v.asInt()) + " is a view handle, not a scene handle"),
              "engine.view.setScene(view, {scene: view}) FAILS by name (it bound the wrong object silently before)");
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
        check(failedWith(eng.call("engine.node.setTransform",
                                  {fakeScene, fakeNode, Value(Value::Object{{"position", Value(Value::Array{Value(nan), Value(0), Value(0)})}})}),
                         "transform.position[0] is not a finite number"),
              "setTransform({position: [NaN, 0, 0]}) FAILS before the engine is asked (it reached setNodeTransform before)");
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
        two.registry().add({"test.ping", {Access::Reads, {}, Kind::Null}, "counts a ping"}, [&pings](Call &) { ++pings; return Value(); });
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

    if (gFail) std::printf("script.host: FAILED (%d)\n", gFail);
    else std::printf("script.host: PASSED\n");
    return gFail ? 1 : 0;
}
