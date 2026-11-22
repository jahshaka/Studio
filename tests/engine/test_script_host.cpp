// script.host — THE ENGINE'S SCRIPT HOST, ITS LAWS ON THE CPU (SCRIPT-IN-ENGINE-1 stage 1; jahshaka/engine/Script.h).
// No device, no engine: a registry of this file's own commands.
// THE LAWS: a value of every kind is itself after a copy and compares by content (an Int equals a Number of the same
// number); every command carries a name and a doc (validate is empty); THE STRICT CALL — an unregistered name, a surplus
// argument, an undeclared object member, a missing argument with no default and a non-integral number for an integer
// each FAIL by name; null and absent take the declared default; an integral number is an integer; a fail is the
// caller's mistake (Failed, null), a refuse the engine's state (Refused, the declared return's falsy value, the message
// kept as lastError).
#include "jahshaka/engine/Script.h"

#include <cstdio>
#include <string>

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
        reg.add({"test.add", {{arg("a", Kind::Int), arg("b", Kind::Int, Value(10))}, Kind::Int}, "a + b"},
                [](Call &c) { return Value(c[0].asInt() + c[1].asInt()); }) &&
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
    check(added && !reg.add({"test.add", {}, "again"}, [](Call &) { return Value(); }),
          "four commands registered; a second command of one name is not");
    check(reg.validate().empty(), "validate is empty: every command has a name, a signature and a doc");
    Registry undocumented;
    undocumented.add({"test.bare", {{arg("", Kind::Int)}, Kind::Null}, ""}, [](Call &) { return Value(); });
    check(undocumented.validate().size() == 2, "validate names a command with no doc and a parameter with no name");

    // 3. THE CALL BY NAME, AND ITS LAWS
    Result r = reg.call("test.add", {Value(2), Value(3)});
    check(r.status == Status::Ok && r.value == Value(5), "test.add(2, 3) = 5 by name");
    check(failedWith(reg.call("test.nothing", {}), "test.nothing is not a command"),
          "an unregistered name FAILS, naming it");
    check(reg.call("test.add", {Value(2)}).value == Value(12) && reg.call("test.add", {Value(2), Value()}).value == Value(12),
          "an absent or null argument takes its declared default");
    check(failedWith(reg.call("test.add", {}), "a is required") && failedWith(reg.call("test.add", {Value()}), "a is required"),
          "an absent or null argument with no default FAILS, naming it");
    check(reg.call("test.add", {Value(3.0), Value(4)}).value == Value(7), "an integral number (3.0) is an integer");
    check(failedWith(reg.call("test.add", {Value(3.7)}), "a must be an integer"),
          "3.7 for an integer FAILS (never truncated to 3)");
    check(failedWith(reg.call("test.add", {Value(1), Value(2), Value(3)}), "3 arguments given, it takes 2"),
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
    check(signatureText(*reg.find("test.add")) == "test.add(a: int, b: int = default) -> int",
          "the signature reads as text: " + signatureText(*reg.find("test.add")));

    if (gFail) std::printf("script.host: FAILED (%d)\n", gFail);
    else std::printf("script.host: PASSED\n");
    return gFail ? 1 : 0;
}
