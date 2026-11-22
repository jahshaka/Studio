// mcp.schema — THE SIGNATURE AS A SCHEMA, ON THE CPU (MCP-ON-HOST-1 stage 1; irisgl/engine/mcp/schema.h says the form).
// No device: the engine's commands registered with NO engine (as jahengine --list registers them), and a synthetic
// registry of every Param form.
// THE LAWS: every Param form maps to its expected text; EVERY registered command maps with no unmapped field to a schema
// the row validates structurally (no command special-cased) and the codec carries both ways; THE THREE REAL SIGNATURES
// (engine.frames, engine.node.setTransform, engine.gi.set) equal, byte for byte, the texts of
// spikes/mcp-on-host/START-PREFLIGHT.md (f); a field set where its kind has no form is a NAMED fault, never dropped (the
// zero-pass control: a synthetic one reads >= 1); and the generator accounts for every field Param and Signature have.
#include "jahshaka/engine/Script.h"
#include "jahshaka/engine/ScriptJson.h"
#include "mcp/schema.h"

#include <cstdio>
#include <set>
#include <string>
#include <type_traits>

using namespace jahshaka::engine;
using namespace jahshaka::engine::script;

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    if (!ok) ++gFail;
}
/// The law judged BEFORE its text is made (argument order is unspecified).
#define CHECK(law, what)                                                                                                  \
    do {                                                                                                                  \
        const bool held = (law);                                                                                          \
        check(held, (what));                                                                                              \
    } while (0)

/// AN AGGREGATE's FIELD COUNT: the most initialisers T{...} takes (Param and Signature are aggregates).
struct AnyField {
    template <class T> operator T() const;
};
template <class T, class... A> constexpr auto takes(int) -> decltype(T{A{}...}, true) { return true; }
template <class T, class... A> constexpr bool takes(...) { return false; }
template <class T, class... A> constexpr int fieldCount()
{
    if constexpr (takes<T, A..., AnyField>(0)) return fieldCount<T, A..., AnyField>();
    else return int(sizeof...(A));
}

/// `info`'s schema as text; "<fault: why>" when it does not map, "<codec: why>" when the codec refuses it.
static std::string schemaText(const CommandInfo &info, std::string &why)
{
    Value s;
    why.clear();
    if (!mcp::argumentSchema(info, s, why)) return "<fault: " + why + ">";
    std::string text;
    if (!toJson(s, text, why)) return "<codec: " + why + ">";
    return text;
}

/// A STRUCTURAL CHECK of one schema (JSON Schema's own rules for the keywords we write): its keys known, "type" one of
/// the seven, properties / items schemas themselves, required naming properties, enum strings, bounds numbers on
/// numbers, item counts ordered. False with `why` naming where.
static bool structural(const Value &s, const std::string &where, std::string &why)
{
    static const std::set<std::string> kKeys = {"type", "description", "properties", "required", "additionalProperties",
                                                "items", "minItems", "maxItems", "enum", "minimum", "exclusiveMinimum", "maximum",
                                                "default",
                                                "propertyNames", "minProperties", "contentEncoding", "maxLength", "const"};
    static const std::set<std::string> kTypes = {"null", "boolean", "integer", "number", "string", "array", "object"};
    if (s.kind() != Kind::Object) return why = where + ": not an object", false;
    for (const Value::Member &m : s.asObject())
        if (!kKeys.count(m.first)) return why = where + ": the keyword '" + m.first + "' is none we write", false;
    const Value &type = s["type"];
    if (type.kind() != Kind::String || !kTypes.count(type.asString())) return why = where + ": no type of the seven", false;
    const std::string t = type.asString();
    const Value &desc = s["description"];
    if (!desc.isNull() && desc.kind() != Kind::String) return why = where + ": a description that is no string", false;
    const Value &props = s["properties"];
    if (!props.isNull()) {
        if (t != "object" || props.kind() != Kind::Object) return why = where + ": properties off an object", false;
        for (const Value::Member &m : props.asObject())
            if (!structural(m.second, where + "." + m.first, why)) return false;
        if (s["additionalProperties"] != Value(false)) return why = where + ": an object open to other members", false;
    }
    const Value &req = s["required"];
    if (!req.isNull()) {
        if (req.kind() != Kind::Array || req.asArray().empty()) return why = where + ": required is no array", false;
        for (const Value &r : req.asArray())
            if (r.kind() != Kind::String || !props.find(r.asString()))
                return why = where + ": required names no property", false;
    }
    const Value &extra = s["additionalProperties"];
    if (extra.kind() == Kind::Object && !structural(extra, where + "{*}", why)) return false;
    const Value &items = s["items"];
    if (t == "array" && (items.isNull() || !structural(items, where + "[]", why)))
        return why = why.empty() ? where + ": an array with no items" : why, false;
    const Value &lo = s["minItems"], &hi = s["maxItems"];
    if ((!lo.isNull() || !hi.isNull()) &&
        (t != "array" || (!lo.isNull() && lo.kind() != Kind::Int) || (!hi.isNull() && hi.kind() != Kind::Int) ||
         (!lo.isNull() && !hi.isNull() && lo.asInt() > hi.asInt())))
        return why = where + ": item counts out of order", false;
    const Value &e = s["enum"];
    if (!e.isNull()) {
        if (e.kind() != Kind::Array || e.asArray().empty()) return why = where + ": an empty enum", false;
        for (const Value &x : e.asArray())
            if (x.kind() != Kind::String) return why = where + ": an enum of no strings", false;
    }
    for (const char *b : {"minimum", "exclusiveMinimum", "maximum"}) {
        const Value &v = s[b];
        if (!v.isNull() && (!(t == "integer" || t == "number") || (v.kind() != Kind::Int && v.kind() != Kind::Number)))
            return why = where + ": a " + b + " off a number", false;
    }
    return true;
}

int main()
{
    std::string why;

    // 1. THE GENERATOR ACCOUNTS FOR EVERY FIELD (a field added to Param or Signature is given its form, never dropped)
    CHECK(fieldCount<Param>() == mcp::kParamFieldsMapped && fieldCount<Signature>() == mcp::kSignatureFieldsMapped,
          "the generator reads every field: Param has " + std::to_string(fieldCount<Param>()) + " (mapped " +
              std::to_string(mcp::kParamFieldsMapped) + "), Signature " + std::to_string(fieldCount<Signature>()) +
              " (mapped " + std::to_string(mcp::kSignatureFieldsMapped) + ")");

    // 2. EVERY PARAM FORM, a synthetic registry
    Registry reg;
    Param others = arg("others", Kind::Array);
    others.element = Kind::Int;
    others.handle = "thing";
    const auto none = [](Call &) { return Value(); };
    const bool added =
        reg.add({"test.forms",
                 {Access::Changes,
                  {target(handle("thing", "thing")),
                   object("opts", {arg("flag", Kind::Bool, Value(true)), arg("count", Kind::Int).range(1, 8).in("px"),
                                   arg("ratio", Kind::Number, Value(0.5)).range(0, 1), arg("mode", Kind::String).oneOf({"a", "b"}),
                                   numbers("vec", 3, Value(Value::Array{Value(0.0), Value(0.0), Value(1.0)})),
                                   objects("items", {arg("k", Kind::String)}), arg("blob", Kind::Bytes),
                                   handle("other", "thing"), others, arg("free", Kind::Array),
                                   arg("raw", Kind::Number).allowNonFinite(), arg("nothing", Kind::Null, Value()),
                                   object("nested", {arg("x", Kind::Number, Value(2.0))}, Value(Value::Object{})),
                                   handles("many", "thing").upTo(5), arg("pos", Kind::Number).above(0)})},
                  Kind::Null},
                 "every form"},
                none) &&
        reg.add({"test.reduce", {Access::Reads, {target(object("pic", {arg("w", Kind::Int)}))}, Kind::Int}, "reduces"}, none) &&
        reg.add({"test.none", {Access::Reads, {}, Kind::Null}, "nothing"}, none);
    CHECK(added && reg.validate().empty(), "the synthetic registry of every Param form registers");
    const std::string forms =
        "{\"type\":\"object\",\"description\":\"every form\",\"properties\":{"
        "\"thing\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a thing handle (target)\"},"
        "\"opts\":{\"type\":\"object\",\"properties\":{"
        "\"flag\":{\"type\":\"boolean\",\"default\":true},"
        "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":8,\"description\":\"px\"},"
        "\"ratio\":{\"type\":\"number\",\"minimum\":0.0,\"maximum\":1.0,\"default\":0.5},"
        "\"mode\":{\"type\":\"string\",\"enum\":[\"a\",\"b\"]},"
        "\"vec\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3,\"default\":[0.0,0.0,1.0]},"
        "\"items\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"properties\":{\"k\":{\"type\":\"string\"}},"
        "\"required\":[\"k\"],\"additionalProperties\":false}},"
        "\"blob\":{\"type\":\"object\",\"propertyNames\":{\"const\":\"$bytes\"},\"minProperties\":1,"
        "\"additionalProperties\":{\"type\":\"string\",\"contentEncoding\":\"base64\",\"maxLength\":1398104},"
        "\"description\":\"bytes as {\\\"$bytes\\\": \\\"<base64>\\\"}, at most 1 MiB\"},"
        "\"other\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a thing handle\"},"
        "\"others\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a thing handle\"}},"
        "\"free\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}},"
        "\"raw\":{\"type\":\"number\",\"description\":\"NaN and infinity allowed (JSON carries neither)\"},"
        "\"nothing\":{\"type\":\"null\",\"default\":null},"
        "\"nested\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"number\",\"default\":2.0}},"
        "\"additionalProperties\":false,\"default\":{\"x\":2.0}},"
        "\"many\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a thing handle\"},"
        "\"maxItems\":5},"
        "\"pos\":{\"type\":\"number\",\"exclusiveMinimum\":0.0}},"
        "\"required\":[\"count\",\"mode\",\"items\",\"blob\",\"other\",\"others\",\"free\",\"raw\",\"many\",\"pos\"],"
        "\"additionalProperties\":false}},\"required\":[\"thing\",\"opts\"],\"additionalProperties\":false}";
    const std::string formsGot = schemaText(*reg.find("test.forms"), why);
    CHECK(formsGot == forms, "every Param form maps to its expected text" + (formsGot == forms ? std::string() : "\n  got  " + formsGot +
                                                                                                    "\n  want " + forms));
    const std::string reduce = "{\"type\":\"object\",\"description\":\"reduces\",\"properties\":{\"pic\":{\"type\":\"object\","
                               "\"properties\":{\"w\":{\"type\":\"integer\"}},\"required\":[\"w\"],\"additionalProperties\":false,"
                               "\"description\":\"the target\"}},\"required\":[\"pic\"],\"additionalProperties\":false}";
    const std::string noneText = "{\"type\":\"object\",\"description\":\"nothing\",\"properties\":{},\"additionalProperties\":false}";
    CHECK(schemaText(*reg.find("test.reduce"), why) == reduce && schemaText(*reg.find("test.none"), why) == noneText,
          "a target that is no handle is 'the target'; a command with no parameter has empty properties and no required (" +
              schemaText(*reg.find("test.reduce"), why) + ")");

    // 3. THE ZERO-PASS CONTROL: a field set where its kind has no form is a NAMED fault (never dropped)
    Param counted = arg("c", Kind::Int);
    counted.count = 2;
    const CommandInfo bad{"test.bad",
                          {Access::Reads,
                           {object("o", {arg("s", Kind::String).range(0, 1), arg("i", Kind::Int).oneOf({"x"}), counted,
                                         arg("m", Kind::Int).upTo(2), arg("bad name!", Kind::Int)})},
                           Kind::Null},
                          "unmappable"};
    Value ignored;
    std::string badWhy;
    const bool badMapped = mcp::argumentSchema(bad, ignored, badWhy);
    int unmappedCount = 0;
    for (std::size_t at = badWhy.find("test.bad: "); at != std::string::npos; at = badWhy.find("test.bad: ", at + 1)) ++unmappedCount;
    CHECK(!badMapped && unmappedCount >= 1 && badWhy.find("o.s: its 'min/max' on a string has no schema form") != std::string::npos &&
              badWhy.find("o.i: its 'choices' on a int has no schema form") != std::string::npos &&
              badWhy.find("o.c: its 'count' on a int has no schema form") != std::string::npos &&
              badWhy.find("o.m: its 'maxCount' on a int has no schema form") != std::string::npos &&
              badWhy.find("the name 'bad name!' is not 1-64") != std::string::npos,
          "the control: unmappable fields read " + std::to_string(unmappedCount) + " named faults (" + badWhy + ")");

    // 4. EVERY REGISTERED COMMAND (no engine: each would FAIL, but every one is listed) - no command special-cased
    Host host;
    registerEngineCommands(host, nullptr, "no device here", false);
    int checked = 0, mapped = 0;
    std::string firstBad;
    for (const CommandInfo *c : host.registry().commands()) {
        ++checked;
        Value s, back;
        std::string w, text, again;
        bool ok = mcp::argumentSchema(*c, s, w) && toJson(s, text, w) && parseJson(text, back, w) && toJson(back, again, w) &&
                  again == text && structural(s, c->name, w) && s["description"] == Value(c->doc);
        if (ok) ++mapped;
        else if (firstBad.empty()) firstBad = c->name + ": " + w;
    }
    CHECK(checked >= 25 && mapped == checked,
          "every registered command (" + std::to_string(checked) + ", the control: >= 25) maps with no unmapped field to a "
          "structurally valid schema the codec carries both ways (" + std::to_string(mapped) + " did" +
              (firstBad.empty() ? std::string() : "; first fault " + firstBad) + ")");

    // 5. THE THREE REAL SIGNATURES, BYTE FOR BYTE (START-PREFLIGHT.md (f); <doc> the command's own doc). engine.gi.set's
    //    text is the preflight's PLUS the bounds SCRIPT-IN-ENGINE-1 2a declared where the preflight found them lost
    //    (commands_gi.cpp: cascades upTo(4), halfSize above(0), resolution 16..256, stepCells 0..128)
    const auto doc = [&](const char *name) {
        std::string t, w;
        toJson(Value(host.registry().find(name)->doc), t, w);
        return t;
    };
    const char *names[] = {"engine.frames", "engine.node.setTransform", "engine.gi.set"};
    const std::string want[] = {
        "{\"type\":\"object\",\"description\":" + doc(names[0]) +
            ",\"properties\":{\"n\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"frames\"}},"
            "\"required\":[\"n\"],\"additionalProperties\":false}",
        "{\"type\":\"object\",\"description\":" + doc(names[1]) +
            ",\"properties\":{\"scene\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a scene handle (target)\"},"
            "\"node\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a node handle (target)\"},"
            "\"transform\":{\"type\":\"object\",\"properties\":{\"position\":{\"type\":\"array\",\"items\":{\"type\":\"number\"},"
            "\"minItems\":3,\"maxItems\":3,\"default\":[0.0,0.0,0.0]},\"rotation\":{\"type\":\"array\",\"items\":{\"type\":"
            "\"number\"},\"minItems\":4,\"maxItems\":4,\"default\":[0.0,0.0,0.0,1.0]},\"scale\":{\"type\":\"array\",\"items\":"
            "{\"type\":\"number\"},\"minItems\":3,\"maxItems\":3,\"default\":[1.0,1.0,1.0]}},\"additionalProperties\":false}},"
            "\"required\":[\"scene\",\"node\",\"transform\"],\"additionalProperties\":false}",
        "{\"type\":\"object\",\"description\":" + doc(names[2]) +
            ",\"properties\":{\"scene\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"a scene handle (target)\"},"
            "\"params\":{\"type\":\"object\",\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"off\",\"vct\","
            "\"vctPccHybrid\"]},\"quality\":{\"type\":\"string\",\"enum\":[\"low\",\"medium\",\"high\",\"epic\"],\"default\":"
            "\"medium\"},\"bounces\":{\"type\":\"boolean\",\"default\":true},\"cascades\":{\"type\":\"array\",\"items\":{\"type\":"
            "\"object\",\"properties\":{\"halfSize\":{\"type\":\"number\",\"exclusiveMinimum\":0.0,\"description\":\"m\"},"
            "\"resolution\":{\"type\":\"integer\",\"minimum\":16,\"maximum\":256,\"description\":\"cells\"},\"stepCells\":"
            "{\"type\":\"number\",\"minimum\":0.0,\"maximum\":128.0,\"description\":\"cells\",\"default\":0.0}},"
            "\"required\":[\"halfSize\",\"resolution\"],\"additionalProperties\":false},\"maxItems\":4}},"
            "\"required\":[\"mode\",\"cascades\"],"
            "\"additionalProperties\":false}},\"required\":[\"scene\",\"params\"],\"additionalProperties\":false}"};
    for (int i = 0; i < 3; ++i) {
        const std::string got = schemaText(*host.registry().find(names[i]), why);
        CHECK(got == want[i], std::string(names[i]) + "'s schema equals the preflight's text byte for byte" +
                                  (got == want[i] ? std::string() : "\n  got  " + got + "\n  want " + want[i]));
    }

    if (gFail) std::printf("mcp.schema: FAILED (%d)\n", gFail);
    else std::printf("mcp.schema: PASSED\n");
    return gFail ? 1 : 0;
}
