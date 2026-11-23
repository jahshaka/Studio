// app.qtstd — THE EDITOR's QtCore CONVERSIONS ARE EXACT (NOQT-DOCUMENT-1 stage 4; src/app/qtstd.h).
//
// The document holds iris::Value; the editor's files, columns and verbs speak QJsonValue and QVariant. Every
// crossing goes through src/app/qtstd.h, so a value that does not survive it is a saved byte that moves.
// THE LAWS: (1) Value <-> QJsonValue and Value <-> QVariant round-trip EXACTLY, kind and bits: an Int stays an Int
// (2^53 + 1 and INT64_MIN, which a double cannot carry), a Number a Number (-0.0's sign, a denormal, 0.1), text in
// UTF-8, nesting; an Object crossing into Qt and back arrives in QJsonObject's / QVariantMap's own (sorted) order.
// QJsonValue's ONE NAMED LIMIT (measured on Qt 6 here, 2026-11-23): QJsonValue(double) STORES AN INTEGRAL double AS
// AN INTEGER (Qt's own constructor), so an integral Number - 3.0, and -0.0 - comes back from a QJsonValue as the Int
// of the same number (Value == equal; -0.0 as 0, the "0" QJsonDocument writes for it either way: no byte moves) -
// iris::json's own option-B law (core/json.h). Through QVariant both keep their kind and bits.
// (2) THE COLOUR's QJsonObject CODEC (RULED A, the spec's DOMAIN COLOUR rule): colorToJson writes the four F
// accessors widened to double, for an INVALID colour too (SrgbColor(): 0, 0, 0, 1; an out-of-range 8-bit
// construction: 0, 0, 0, 0 - pinned by name); colorFromJson clamps each channel to [0, 1], reads `a` absent as 1,
// and reads an EMPTY object as the caller's `absent`.
// CPU only: no engine, no display.
#include "app/qtstd.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QVariant>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

static int failures = 0;
static int cases = 0;
#define CHECK(cond, msg) do { ++cases; if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// The same kind and the same bits (a Number compared by its bits: -0.0 is not 0.0 here, unlike Value ==).
static bool exact(const iris::Value &a, const iris::Value &b)
{
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
    case iris::Kind::Null:   return true;
    case iris::Kind::Bool:   return a.asBool() == b.asBool();
    case iris::Kind::Int:    return a.asInt() == b.asInt();
    case iris::Kind::Number: {
        const double x = a.asNumber(), y = b.asNumber();
        return std::memcmp(&x, &y, sizeof x) == 0;
    }
    case iris::Kind::String: return a.asString() == b.asString();
    case iris::Kind::Bytes:  return a.asBytes() == b.asBytes();
    case iris::Kind::Array: {
        if (a.asArray().size() != b.asArray().size()) return false;
        for (size_t i = 0; i < a.asArray().size(); ++i)
            if (!exact(a.asArray()[i], b.asArray()[i])) return false;
        return true;
    }
    case iris::Kind::Object: {
        if (a.asObject().size() != b.asObject().size()) return false;
        for (size_t i = 0; i < a.asObject().size(); ++i)
            if (a.asObject()[i].first != b.asObject()[i].first || !exact(a.asObject()[i].second, b.asObject()[i].second))
                return false;
        return true;
    }
    }
    return false;
}

static void testScalars()
{
    std::printf("\n-- (1) scalars cross both ways, kind and bits --\n");
    const std::int64_t p53plus1 = (std::int64_t(1) << 53) + 1;
    struct Case { const char *what; iris::Value v; };
    const Case values[] = {
        { "null", iris::Value() },
        { "true", iris::Value(true) },
        { "2^53 + 1 as an Int (a double cannot carry it)", iris::Value(p53plus1) },
        { "INT64_MIN as an Int", iris::Value(std::numeric_limits<std::int64_t>::min()) },
        { "-0.0 as a Number (its sign; QVariant only: QJsonValue's limit below)", iris::Value(-0.0) },
        { "the smallest denormal as a Number", iris::Value(std::numeric_limits<double>::denorm_min()) },
        { "0.1 as a Number", iris::Value(0.1) },
        { "3.0 as a Number (QVariant only: QJsonValue's limit below)", iris::Value(3.0) },
        { "UTF-8 text (non-ASCII)", iris::Value(std::string("caf\xc3\xa9 \xe2\x82\xac")) },
    };
    for (const Case &c : values) {
        char msg[200];
        const bool integralNumber = c.v.kind() == iris::Kind::Number && c.v.asNumber() == std::trunc(c.v.asNumber());
        std::snprintf(msg, sizeof msg, "QJsonValue round trip: %s", c.what);
        if (integralNumber) {
            const iris::Value back = qtstd::toValue(qtstd::toQJsonValue(c.v));
            std::snprintf(msg, sizeof msg, "QJsonValue: %s comes back as the Int of the same number (Qt's limit)", c.what);
            CHECK(back.kind() == iris::Kind::Int && back == c.v, msg);
        } else {
            CHECK(exact(qtstd::toValue(qtstd::toQJsonValue(c.v)), c.v), msg);
        }
        std::snprintf(msg, sizeof msg, "QVariant round trip: %s", c.what);
        CHECK(exact(qtstd::fromVariant(qtstd::toVariant(c.v)), c.v), msg);
    }
    CHECK(qtstd::toQJsonValue(iris::Value(p53plus1)).toInteger() == p53plus1,
          "the 2^53 + 1 case: QJsonValue holds the integer itself (toInteger)");
    CHECK(qtstd::toValue(QJsonValue(qint64(7))).kind() == iris::Kind::Int &&
          qtstd::toValue(QJsonValue(7.5)).kind() == iris::Kind::Number,
          "a QJsonValue integer reads as an Int, a fraction as a Number");
    CHECK(qtstd::toVariant(iris::Value()).isValid() == false, "Null <-> an INVALID QVariant");
}

static void testContainers()
{
    std::printf("\n-- (1) arrays and objects: nested; an Object arrives in Qt's (sorted) order --\n");
    const iris::Value nested(iris::Value::Array{
        iris::Value(1), iris::Value(2.5), iris::Value("x"),
        iris::Value(iris::Value::Array{iris::Value(true), iris::Value()}),
        iris::Value(iris::Value::Object{{"k", iris::Value(iris::Value::Array{iris::Value(-0.5)})}})});
    CHECK(exact(qtstd::toValue(qtstd::toQJsonValue(nested)), nested), "a nested Array crosses QJsonValue exactly");
    CHECK(exact(qtstd::fromVariant(qtstd::toVariant(nested)), nested), "a nested Array crosses QVariant exactly");

    const iris::Value unsorted(iris::Value::Object{{"b", iris::Value(1)}, {"a", iris::Value(2)}, {"c", iris::Value(3)}});
    const iris::Value sorted(iris::Value::Object{{"a", iris::Value(2)}, {"b", iris::Value(1)}, {"c", iris::Value(3)}});
    const iris::Value viaJson = qtstd::toValue(qtstd::toQJsonValue(unsorted));
    const iris::Value viaVariant = qtstd::fromVariant(qtstd::toVariant(unsorted));
    CHECK(exact(viaJson, sorted), "an Object {b, a, c} back from a QJsonObject is {a, b, c}: QJsonObject's order");
    CHECK(exact(viaVariant, sorted), "...and from a QVariantMap the same: QVariantMap's order");
    const QJsonObject o = qtstd::toQJsonValue(unsorted).toObject();
    CHECK(o.keys() == QStringList({"a", "b", "c"}) && o.value("b").toInteger() == 1,
          "toQJsonValue: the QJsonObject holds every member, keyed and valued");
    CHECK(qtstd::toVariantMap(unsorted).value("c").toLongLong() == 3 && qtstd::toVariantMap(iris::Value(1)).isEmpty(),
          "toVariantMap: an Object's members; any other kind an empty map");
}

static void testColourCodec()
{
    std::printf("\n-- (2) the colour's QJsonObject codec --\n");
    const iris::SrgbColor c = iris::SrgbColor::fromRgba64(1, 32768, 65535, 4660);
    const QJsonObject o = qtstd::colorToJson(c);
    CHECK(o.size() == 4 && o.value("r").toDouble() == double(c.redF()) && o.value("g").toDouble() == double(c.greenF()) &&
          o.value("b").toDouble() == double(c.blueF()) && o.value("a").toDouble() == double(c.alphaF()),
          "colorToJson writes the four F accessors widened to double");
    const QJsonObject inv = qtstd::colorToJson(iris::SrgbColor());
    CHECK(inv.value("r").toDouble() == 0.0 && inv.value("g").toDouble() == 0.0 && inv.value("b").toDouble() == 0.0 &&
          inv.value("a").toDouble() == 1.0, "THE invalid colour SrgbColor() writes r 0, g 0, b 0, a 1");
    const QJsonObject bad = qtstd::colorToJson(iris::SrgbColor(300, 0, 0));
    CHECK(!iris::SrgbColor(300, 0, 0).isValid() && bad.value("r").toDouble() == 0.0 && bad.value("g").toDouble() == 0.0 &&
          bad.value("b").toDouble() == 0.0 && bad.value("a").toDouble() == 0.0,
          "an out-of-range 8-bit construction writes r 0, g 0, b 0, a 0");
    CHECK(qtstd::colorFromJson(o) == c, "colorFromJson reads colorToJson's object back to the same colour");
    const iris::SrgbColor clamped = qtstd::colorFromJson(QJsonObject{{"r", 2.0}, {"g", -1.0}, {"b", 0.5}});
    CHECK(clamped == iris::SrgbColor::fromRgbF(1.0f, 0.0f, 0.5f, 1.0f),
          "colorFromJson clamps each channel to [0, 1] and reads `a` absent as 1");
    const iris::SrgbColor absent(1, 2, 3);
    CHECK(qtstd::colorFromJson(QJsonObject(), absent) == absent && !qtstd::colorFromJson(QJsonObject()).isValid(),
          "an EMPTY object reads as the caller's absent (the invalid colour by default)");
    CHECK(qtstd::colorFromJson(QJsonObject{{"r", 1}}, absent) == iris::SrgbColor::fromRgbF(1.0f, 0.0f, 0.0f, 1.0f),
          "an integer channel reads as its number (one JSON number type)");
}

int main()
{
    testScalars();
    testContainers();
    testColourCodec();
    std::printf("\ncases %d (> 0)\n", cases);
    if (cases == 0) ++failures;
    if (failures) std::printf("app.qtstd: FAILED (%d)\n", failures);
    else std::printf("app.qtstd: PASSED\n");
    return failures ? 1 : 0;
}
