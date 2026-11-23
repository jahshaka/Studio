// core.json — THE ONE JSON CODEC, ITS LAWS ON THE CPU (MCP-ON-HOST-1 stage 1 as script.json; irisgl/core/json.h says
// them). No device, no engine: the codec alone, in the core library with the value (NOQT-CORE-1 stage 3: moved).
// THE LAWS: every kind round-trips value -> text -> value, its VALUE (ONE NUMBER TEXT, option B, Qt's: an integral Number
// is written "1" and reads back an Int - a Number parameter admits it, script.host); 0.1 + 0.2 by its bits; -0.0 written
// "0" (Qt's); 2^53 + 1 and INT64_MIN exact as Int; an integer past int64 and a number past a double refused; NaN
// and infinity never written (the path named); strict UTF-8 both ways (`\u` pairs joined, a lone surrogate refused);
// control characters and `\n` written escaped; a duplicate member, nesting past 64, trailing text, a BOM and an empty
// input refused (line and column named); bytes as {"$bytes": base64} both ways, capped at 1 MiB by name.
#include "irisgl/core/json.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

using namespace iris;
using namespace iris::json;

static int gFail = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", what.c_str());
    if (!ok) ++gFail;
}

/// The law judged BEFORE its text is made (a message quoting `why` must quote the law's own refusal: argument order is
/// unspecified).
#define CHECK(law, what)                                                                                                  \
    do {                                                                                                                  \
        const bool held = (law);                                                                                          \
        check(held, (what));                                                                                              \
    } while (0)

/// Equal AND of the same kind all the way down (Value::operator== takes an Int for a Number of the same number).
static bool sameKind(const Value &a, const Value &b)
{
    if (a.kind() != b.kind() || a != b) return false;
    if (a.kind() == Kind::Array)
        for (std::size_t i = 0; i < a.asArray().size(); ++i)
            if (!sameKind(a.asArray()[i], b.asArray()[i])) return false;
    if (a.kind() == Kind::Object)
        for (std::size_t i = 0; i < a.asObject().size(); ++i)
            if (a.asObject()[i].first != b.asObject()[i].first || !sameKind(a.asObject()[i].second, b.asObject()[i].second))
                return false;
    return true;
}
static std::uint64_t bits(double d)
{
    std::uint64_t u = 0;
    std::memcpy(&u, &d, sizeof u);
    return u;
}
static std::string text(const Value &v)
{
    std::string out, why;
    return json::write(v, out, why) ? out : "<refused: " + why + ">";
}
/// The value `t` reads as; `why` empty when it read.
static Value read(const std::string &t, std::string &why)
{
    Value v;
    why.clear();
    if (!json::parse(t, v, why) && why.empty()) why = "<refused with no reason>";
    return v;
}
static bool refused(const std::string &t, const std::string &part, std::string &why)
{
    Value v;
    why.clear();
    return !json::parse(t, v, why) && why.find(part) != std::string::npos;
}
static bool writeRefused(const Value &v, const std::string &part, std::string &why)
{
    std::string out;
    why.clear();
    return !json::write(v, out, why) && out.empty() && why.find(part) != std::string::npos;
}

int main()
{
    std::string why;

    // 1. EVERY KIND ROUND-TRIPS (its value; a Number's kind only when it is not integral: option B)
    const Value all(Value::Object{
        {"null", Value()}, {"yes", Value(true)}, {"no", Value(false)}, {"int", Value(-42)}, {"one", Value(1.0)},
        {"zero", Value(0.0)}, {"big", Value(1e300)}, {"tiny", Value(5e-324)}, {"half", Value(2.5)},
        {"text", Value("utf-8 \xc3\xa9 \xf0\x9f\x98\x80 \"q\" \\ /")},
        {"list", Value(Value::Array{Value(1), Value(1.0), Value("two"), Value(), Value(Value::Array{})})},
        {"nest", Value(Value::Object{{"b", Value(2)}, {"a", Value(Value::Object{})}})},
        {"bytes", Value::bytes({0, 1, 2, 254, 255})}});
    const std::string allText = text(all);
    const Value back = read(allText, why);
    CHECK(why.empty() && back == all && back["half"].kind() == Kind::Number && back["tiny"].kind() == Kind::Number &&
              back["one"].kind() == Kind::Int && sameKind(back["text"], all["text"]) && sameKind(back["bytes"], all["bytes"]),
          "every kind round-trips value -> text -> value, its value; a non-integral Number stays a Number, an integral one "
          "reads back an Int (" + allText.substr(0, 96) + "...) " + why);
    CHECK(text(Value(1.0)) == "1" && text(Value(0.0)) == "0" && text(Value(1e300)) == "1e+300" && text(Value(1)) == "1" &&
              text(Value(1e16)) == "1e+16" && text(Value(1e-4)) == "1e-04" && text(Value(0.001)) == "0.001" &&
              read("1.0", why).kind() == Kind::Number && read("1", why).kind() == Kind::Int &&
              read("1e2", why).kind() == Kind::Number,
          "ONE NUMBER TEXT (option B, Qt's): an integral Number of magnitude <= 2^53 as an integer (1.0 " + text(Value(1.0)) +
              "), any other the shortest of fixed and exponent (1e300 " + text(Value(1e300)) + ", 1e16 " +
              text(Value(1e16)) + ", 1e-4 " + text(Value(1e-4)) + ", 0.001 " + text(Value(0.001)) +
              "); an integer literal reads an Int, a fraction or an exponent a Number");
    CHECK(text(Value(Value::Object{{"b", Value(1)}, {"a", Value(2)}})) == "{\"b\":1,\"a\":2}" &&
              read("{\"b\":1,\"a\":2}", why).asObject()[0].first == "b",
          "an object keeps its members' order both ways");

    // 2. NUMBERS BY THEIR BITS
    const double sum = 0.1 + 0.2, negZero = -0.0;
    const Value sumBack = read(text(Value(sum)), why), zeroBack = read(text(Value(negZero)), why);
    CHECK(sumBack.kind() == Kind::Number && bits(sumBack.asNumber()) == bits(sum),
          "0.1 + 0.2 round-trips bit-exact (" + text(Value(sum)) + ")");
    CHECK(text(Value(negZero)) == "0" && zeroBack == Value(negZero),
          "-0.0 is written \"0\" as Qt writes it (its sign dropped; equal as a number) (" + text(Value(negZero)) + ")");
    const std::int64_t p53 = (std::int64_t(1) << 53) + 1, lowest = std::numeric_limits<std::int64_t>::min();
    const Value p53Back = read("9007199254740993", why), lowBack = read("-9223372036854775808", why);
    CHECK(p53Back.kind() == Kind::Int && p53Back.asInt() == p53 && lowBack.kind() == Kind::Int && lowBack.asInt() == lowest &&
              read(text(Value(p53)), why).asInt() == p53 && read(text(Value(lowest)), why).asInt() == lowest,
          "2^53+1 and INT64_MIN are exact as Int both ways");
    CHECK(refused("9223372036854775808", "outside a 64-bit integer", why), "an integer past int64 is refused (" + why + ")");
    CHECK(refused("1e400", "outside a double's range", why), "a number past a double's range is refused (" + why + ")");
    const Value tiny = read("4.9e-324", why);
    CHECK(refused("1e-400", "outside a double's range", why) && refused("-1e-400", "outside a double's range", why) &&
              tiny.kind() == Kind::Number && bits(tiny.asNumber()) == 1,
          "an UNDERFLOW is refused, never read as zero (" + why + "); the smallest subnormal 4.9e-324 reads exactly (bits 1)");
    CHECK(refused("01", "leading zero", why) && refused("+1", "begins no value", why) && refused("1.", "after '.'", why),
          "JSON's number grammar: no leading zero, no '+', a digit after '.' (" + why + ")");
    const Value nan(Value::Object{{"transform", Value(Value::Object{{"position", Value(Value::Array{
                                                    Value(0.0), Value(std::numeric_limits<double>::quiet_NaN()), Value(0.0)})}})}});
    CHECK(writeRefused(nan, "value.transform.position[1] is not a finite number", why),
          "NaN in an answer FAILS naming its path (" + why + ")");
    CHECK(writeRefused(Value(Value::Array{Value(-std::numeric_limits<double>::infinity())}), "value[0] is not a finite", why),
          "an infinity FAILS naming its path (" + why + ")");

    // 3. STRICT UTF-8, BOTH WAYS
    const char *badUtf8[] = {"\"\xC0\xAF\"", "\"\xED\xA0\x80\"", "\"\xF4\x90\x80\x80\"", "\"\x80\""};
    const char *badName[] = {"overlong C0 AF", "surrogate ED A0 80", "over U+10FFFF F4 90 80 80", "stray continuation 80"};
    for (int i = 0; i < 4; ++i)
        CHECK(refused(badUtf8[i], "invalid UTF-8", why), std::string("read refuses ") + badName[i] + " (" + why + ")");
    CHECK(refused("\"\\uD800\"", "lone high surrogate", why) && refused("\"\\uDC00x\"", "lone low surrogate", why),
          "a lone surrogate escape is refused (" + why + ")");
    CHECK(read("\"\\uD83D\\uDE00\"", why).asString() == "\xF0\x9F\x98\x80" && why.empty(),
          "a \\u surrogate pair is joined into one code point (U+1F600)");
    CHECK(writeRefused(Value(Value::Array{Value("ok"), Value("\xC0\xAF")}), "value[1] holds invalid UTF-8", why),
          "an answer holding invalid UTF-8 FAILS naming its path (" + why + ")");
    CHECK(text(Value("a\nb")) == "\"a\\nb\"" && text(Value(std::string("\x01\x1f\t", 3))) == "\"\\u0001\\u001f\\t\"" &&
              read("\"a\\nb\"", why).asString() == "a\nb",
          "control characters and \\n are written escaped: no written line holds a newline (" + text(Value("a\nb")) + ")");
    CHECK(refused("\"a\nb\"", "raw control character", why), "a raw newline inside a string is refused (" + why + ")");

    // 4. THE TEXT's SHAPE
    CHECK(refused("{\"a\":1,\"a\":2}", "the member 'a' is named twice", why),
          "a duplicate member is refused (" + why + ")");
    std::string deep64, deep65;
    for (int i = 0; i < 64; ++i) deep64 = "[" + deep64 + "]";
    deep65 = "[" + deep64 + "]";
    Value nested = Value(Value::Array{});
    for (int i = 0; i < 64; ++i) nested = Value(Value::Array{nested});
    CHECK(read(deep64, why).kind() == Kind::Array && why.empty() && refused(deep65, "nested deeper than 64", why) &&
              writeRefused(nested, "nested deeper than 64", why),
          "64 levels of nesting read; 65 are refused both ways (" + why + ")");
    CHECK(refused("1 2", "line 1, column 3: text follows the value", why), "trailing text is refused (" + why + ")");
    CHECK(refused("\xEF\xBB\xBF{}", "byte-order mark", why), "a byte-order mark is refused (" + why + ")");
    CHECK(refused("", "empty input", why) && refused("   ", "ends where a value belongs", why),
          "an empty input is refused (" + why + ")");
    CHECK(refused("{\n  \"a\": tru }", "line 2, column 8:", why), "a refusal names its line and column (" + why + ")");
    CHECK(refused("[1,]", "begins no value", why) && refused("{\"a\":1,}", "a member's name", why),
          "a trailing comma is refused (" + why + ")");
    CHECK(refused("\"\\x\"", "an unknown escape '\\x'", why), "an unknown escape is refused (" + why + ")");

    // 5. BYTES
    const Value b = Value::bytes({0, 1, 2, 255});
    CHECK(text(b) == "{\"$bytes\":\"AAEC/w==\"}" && sameKind(read(text(b), why), b) &&
              sameKind(read("{\"$bytes\":\"\"}", why), Value::bytes({})) &&
              sameKind(read("{\"$bytes\":\"AAE=\"}", why), Value::bytes({0, 1})),
          "bytes are {\"$bytes\": base64} both ways (" + text(b) + ")");
    CHECK(refused("{\"$bytes\": 5}", "must be exactly", why) && refused("{\"$bytes\":\"AA==\",\"y\":1}", "must be exactly", why),
          "{\"$bytes\": 5} and {\"$bytes\": .., \"y\": 1} are refused (" + why + ")");
    CHECK(refused("{\"$bytes\":\"A===\"}", "not base64", why) && refused("{\"$bytes\":\"AB==\"}", "canonical", why) &&
              refused("{\"$bytes\":\"AA\"}", "multiple of 4", why),
          "the $bytes text is strict base64 (" + why + ")");
    CHECK(writeRefused(Value(Value::Object{{"$bytes", Value("AA==")}}), "has a member named \"$bytes\"", why),
          "an object with a member $bytes FAILS to write (it would read back as bytes) (" + why + ")");
    const Value::Bytes mib(kBytesCap, 7);
    Value::Bytes over(kBytesCap + 1, 7);
    const std::string mibText = text(Value::bytes(mib));
    // 1 MiB = 3 x 349525 + 1: the cap's text ends in the one byte's group "Bw=="; one byte more makes it "Bwc="
    const std::string overText = mibText.substr(0, mibText.size() - 6) + "Bwc=\"}";
    CHECK(mibText.size() > 6 && mibText.compare(mibText.size() - 6, 6, "Bw==\"}") == 0 &&
              sameKind(read(mibText, why), Value::bytes(mib)) && why.empty(),
          "exactly 1 MiB of bytes is carried both ways (the control)");
    CHECK(writeRefused(Value::bytes(over), "1048577 bytes: over the 1 MiB", why) &&
              refused(overText, "1048577 bytes: over the 1 MiB", why),
          "1 MiB + 1 bytes are refused by name both ways (" + why + ")");

    if (gFail) std::printf("core.json: FAILED (%d)\n", gFail);
    else std::printf("core.json: PASSED\n");
    return gFail ? 1 : 0;
}
