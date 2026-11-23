// document.property_rows — THE STRICT DOCUMENT AND THE CONVERTING EDGE (NOQT-DOCUMENT-1 stage 4; the spec's RULED 20 and
// calls 5 and E; src/app/qtstdgui.h, irisgl/document/properties/property.h).
//
// THE LAWS:
// (1) ROUND TRIP: every row of a PbrMaterial (and every row class on its own) takes back the Value it gives:
//     getValue -> setValue -> getValue is the same Value, the texture row in BOTH shapes (a path String; a
//     {guid, path} Object, whose guid the row then names).
// (2) A WRONG KIND IS REFUSED AT THE DOCUMENT: for every row, every Value kind that is not the row's own answers
//     false and moves NOTHING — every row's value, every texture row's guid, the material's revision and its
//     fields (a fingerprint of them) are what they were. The kinds a row takes: Bool a Bool; Int and List an Int;
//     Float a Number OR AN Int (RULED E (1): one JSON number type); Color an Object; Vec3 an Array of three numbers;
//     Texture a String or an Object; File a String. AN INTEGRAL Number INTO AN Int ROW IS REFUSED (RULED E).
// (3) THE SAME INPUT THROUGH THE EDGE GIVES WHAT QT GIVES: for every row kind and a set of QVariants (an int, a
//     double, the strings "3" and "abc", a bool, an RGB and an HSV QColor, a QVector3D, a {path, guid} map, an
//     invalid QVariant), qtstd::toRowValue then the row's setter leaves the field Qt's own call makes of it
//     (toBool / toInt / toFloat / value<QColor> / value<QVector3D> / toString, computed HERE by Qt), and WHAT QT
//     CANNOT CONVERT IS REFUSED (the lead's ruling, 2026-11-23: a number that does not read, an invalid QColor, a
//     non-vector, an invalid QVariant): the edge answers a Null Value and the row refuses it, unchanged. Qt's own
//     conversions, pinned with their measured values: 3.7 -> 4, "3" -> 3, true -> 1, 4294967295.0 -> -1.
// (4) THE COLOUR's VALUE SHAPE over all 65,536 16-bit values: colourFromValue(colourToValue(c)) == c for
//     SrgbColor::fromRgba64(c, c, c, c); the invalid colour reads back as `absent` only from an empty Object or a
//     Null (its own shape reads as black: the four numbers RULED B keeps).
// CONTROLS: rows exercised > 0 per row class; edge inputs > 0 per kind; colours checked = 65,536.
// CPU only: the document's own types, no engine, no display (Qt6::Gui is the edge under test and the reference).
#include "app/qtstd.h"
#include "app/qtstdgui.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/properties/property.h"
#include "irisgl/document/properties/valueshapes.h"

#include <QColor>
#include <QVariant>
#include <QVariantMap>
#include <QVector3D>

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", std::string(msg).c_str()); \
    else { std::printf("FAIL: %s\n", std::string(msg).c_str()); ++failures; } } while (0)
/// A failure printed always, a pass counted only (the loops make thousands).
#define QUIET(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", std::string(msg).c_str()); ++failures; } } while (0)

static const char *typeName(iris::PropertyType t)
{
    switch (t) {
    case iris::PropertyType::Bool: return "Bool";
    case iris::PropertyType::Int: return "Int";
    case iris::PropertyType::Float: return "Float";
    case iris::PropertyType::Vec3: return "Vec3";
    case iris::PropertyType::Color: return "Color";
    case iris::PropertyType::Texture: return "Texture";
    case iris::PropertyType::File: return "File";
    case iris::PropertyType::List: return "List";
    case iris::PropertyType::None: break;
    }
    return "None";
}

/// The kinds a row of `t` takes (law 2's table).
static bool takes(iris::PropertyType t, const iris::Value &v)
{
    switch (t) {
    case iris::PropertyType::Bool:    return v.kind() == iris::Kind::Bool;
    case iris::PropertyType::Int:
    case iris::PropertyType::List:    return v.kind() == iris::Kind::Int;
    case iris::PropertyType::Float:   return v.kind() == iris::Kind::Int || v.kind() == iris::Kind::Number;
    case iris::PropertyType::Color:   return v.kind() == iris::Kind::Object;
    case iris::PropertyType::Vec3: {
        if (v.kind() != iris::Kind::Array || v.asArray().size() != 3) return false;
        for (const iris::Value &c : v.asArray())
            if (c.kind() != iris::Kind::Int && c.kind() != iris::Kind::Number) return false;
        return true;
    }
    case iris::PropertyType::Texture: return v.kind() == iris::Kind::String || v.kind() == iris::Kind::Object;
    case iris::PropertyType::File:    return v.kind() == iris::Kind::String;
    case iris::PropertyType::None:    break;
    }
    return false;
}

/// One Value of every kind (and the integral Number RULED E names).
static std::vector<std::pair<const char *, iris::Value>> everyKind()
{
    return {
        { "Null", iris::Value() },
        { "Bool", iris::Value(true) },
        { "Int", iris::Value(1) },
        { "Number 0.5", iris::Value(0.5) },
        { "integral Number 3.0", iris::Value(3.0) },
        { "String", iris::Value("s") },
        { "Array of 3 numbers", iris::Value(iris::Value::Array{iris::Value(1), iris::Value(2.0), iris::Value(3)}) },
        { "Array of 2", iris::Value(iris::Value::Array{iris::Value(1), iris::Value(2)}) },
        { "Object", iris::Value(iris::Value::Object{{"path", iris::Value("p")}}) },
        { "Object with a non-string path", iris::Value(iris::Value::Object{{"path", iris::Value(1)}}) },
    };
}

/// A fingerprint of a PbrMaterial's fields: every field setValue reaches, as text.
static std::string fields(const iris::PbrMaterial &m)
{
    char b[1024];
    const auto c = [](const iris::SrgbColor &x) {
        return (unsigned long long)x.red16() << 48 | (unsigned long long)x.green16() << 32 |
               (unsigned long long)x.blue16() << 16 | x.alpha16() | (x.isValid() ? 0 : 1ull);
    };
    std::snprintf(b, sizeof b, "%llx %llx %llx %llx %g %g %g %g %g %g %g %g %g %g %g %g %g %g %d %g %g %g %d %d %g %d %d "
                  "%g %d %d %d %g %g %s %s %d",
                  c(m.baseColor), c(m.emissiveColor), c(m.specularColor), c(m.fresnelColor), m.metallicFactor,
                  m.roughnessFactor, m.normalFactor, m.emissiveIntensity, m.alpha, m.textureScale, m.textureScaleV,
                  m.textureOffsetU, m.textureOffsetV, m.textureRotation, m.roughnessLowerBound, m.roughnessUpperBound,
                  m.alphaCutoff, m.refractionStrength, m.alphaMode, m.clearCoat, m.clearCoatRoughness, m.ior,
                  m.shadingModel, m.workflow, m.anisotropy, int(m.useFresnelColor), int(m.separateFresnel),
                  m.textureVelocityU, m.brdf, int(m.receiveShadows), int(m.emissiveAsLightmap), m.textureVelocityV,
                  m.detail[0].weight, m.customPiecePixel.toStdString().c_str(), m.customPieceVertex.toStdString().c_str(),
                  int(m.textures.size()));
    std::string out = b;
    for (int l = 0; l < iris::PbrMaterial::kDetailLayers; ++l) {
        const auto &d = m.detail[l];
        std::snprintf(b, sizeof b, " [%d %g %g %g %g %g %g]", d.blend, d.offsetU, d.offsetV, d.scaleU, d.scaleV, d.weight,
                      d.normalWeight);
        out += b;
    }
    for (const QString &row : iris::PbrMaterial::mapRowNames()) out += " " + std::to_string(m.addressFor(row));
    return out;
}

/// Every row's Value and every texture row's guid, in order.
static std::vector<iris::Value> rows(const iris::PbrMaterial &m)
{
    std::vector<iris::Value> out;
    for (const iris::Property *p : m.properties()) {
        out.push_back(p->getValue());
        if (p->type == iris::PropertyType::Texture) out.push_back(iris::Value(m.textureGuid(p->name)));
    }
    return out;
}

static void testMaterialRows()
{
    std::printf("\n-- (1) and (2): every PbrMaterial row: its own Value back; every other kind refused, nothing moved --\n");
    auto mat = iris::PbrMaterial::create();
    std::map<std::string, int> perType;
    int roundTrips = 0, refusals = 0, accepted = 0;
    for (iris::Property *prop : mat->properties()) {
        ++perType[typeName(prop->type)];
        const iris::Value own = prop->getValue();
        QUIET(mat->setValue(prop->name, own) && prop->getValue() == own, prop->name + ": its own Value round-trips");
        ++roundTrips;
        if (prop->type == iris::PropertyType::Texture) {
            const iris::Value ref = iris::Material::textureRef("/no/such/dir/map.png", "guid-" + prop->name);
            QUIET(mat->setValue(prop->name, ref) && prop->getValue() == iris::Value("/no/such/dir/map.png") &&
                  mat->textureGuid(prop->name) == "guid-" + prop->name &&
                  mat->textureRefOf(prop->name) == ref,
                  prop->name + ": the {guid, path} Object binds both, and textureRefOf gives it back");
            QUIET(mat->setValue(prop->name, iris::Value("/no/such/dir/map.png")) &&
                  mat->textureGuid(prop->name) == "guid-" + prop->name,
                  prop->name + ": the same path as a String keeps the guid");
            QUIET(mat->setValue(prop->name, iris::Value("/no/such/dir/other.png")) && mat->textureGuid(prop->name).empty(),
                  prop->name + ": another path as a String binds a file nobody named: the guid goes");
            roundTrips += 3;
        }
        for (const auto &k : everyKind()) {
            if (takes(prop->type, k.second)) { ++accepted; continue; }
            const std::vector<iris::Value> before = rows(*mat);
            const std::string fieldsBefore = fields(*mat);
            const auto rev = mat->revision();
            const bool set = mat->setValue(prop->name, k.second);
            QUIET(!set && rows(*mat) == before && fields(*mat) == fieldsBefore && mat->revision() == rev,
                  prop->name + " (" + typeName(prop->type) + ") refuses " + k.first + ", nothing moved");
            ++refusals;
        }
    }
    CHECK(!mat->setValue("noSuchRow", iris::Value(1.0)) && !mat->setValue("detail9Weight", iris::Value(1.0)),
          "an unknown name (and a detail layer past the last) is refused");
    CHECK(mat->setValue("alpha", iris::Value(1)) && mat->alpha == 1.0f,
          "RULED E (1): a Float row takes an Int as its number");
    const int alphaMode = mat->alphaMode;
    CHECK(!mat->setValue("alphaMode", iris::Value(2.0)) && mat->alphaMode == alphaMode,
          "RULED E: an integral Number into an Int / List row is REFUSED");
    CHECK(mat->setValue("textureVelocityU", iris::Value(0.25)) && mat->textureVelocityU == 0.25f &&
          !mat->setValue("textureVelocityU", iris::Value("x")),
          "a field with no row takes its own kind (a Number) and refuses another");
    std::printf("rows: round trips %d, refusals %d, accepted kinds skipped %d\n", roundTrips, refusals, accepted);
    // (PbrMaterial has no Int row - its enums are List rows; the node classes carry the Int rows.)
    for (const char *t : {"Bool", "Float", "Color", "Texture", "List"})
        CHECK(perType[t] > 0, std::string("control: PbrMaterial rows of class ") + t + " exercised: " + std::to_string(perType[t]));
    CHECK(refusals > 0, "control: refusals checked > 0");
}

/// A fresh row of each kind (the classes no material row reaches - Vec3, File - included).
static std::unique_ptr<iris::Property> rowOf(iris::PropertyType t)
{
    switch (t) {
    case iris::PropertyType::Bool:    return std::make_unique<iris::BoolProperty>();
    case iris::PropertyType::Int:     return std::make_unique<iris::IntProperty>();
    case iris::PropertyType::Float:   return std::make_unique<iris::FloatProperty>();
    case iris::PropertyType::Vec3:    return std::make_unique<iris::Vec3Property>();
    case iris::PropertyType::Color:   return std::make_unique<iris::ColorProperty>();
    case iris::PropertyType::Texture: return std::make_unique<iris::TextureProperty>();
    case iris::PropertyType::File:    return std::make_unique<iris::FileProperty>();
    case iris::PropertyType::List:    return std::make_unique<iris::ListProperty>();
    case iris::PropertyType::None:    break;
    }
    return nullptr;
}

static const iris::PropertyType kKinds[] = {
    iris::PropertyType::Bool, iris::PropertyType::Int, iris::PropertyType::Float, iris::PropertyType::Vec3,
    iris::PropertyType::Color, iris::PropertyType::Texture, iris::PropertyType::File, iris::PropertyType::List };

static void testRowClasses()
{
    std::printf("\n-- (1) and (2) for every row class on its own --\n");
    for (iris::PropertyType t : kKinds) {
        auto row = rowOf(t);
        const iris::Value own = row->getValue();
        CHECK(row->setValue(own) && row->getValue() == own, std::string(typeName(t)) + ": its own Value round-trips");
        int refused = 0;
        for (const auto &k : everyKind()) {
            if (takes(t, k.second)) continue;
            const iris::Value before = row->getValue();
            QUIET(!row->setValue(k.second) && row->getValue() == before,
                  std::string(typeName(t)) + " refuses " + k.first);
            ++refused;
        }
        CHECK(refused > 0, std::string(typeName(t)) + ": " + std::to_string(refused) + " wrong kinds refused");
    }
    iris::Vec3Property v;
    CHECK(v.setValue(iris::vec3ToValue(iris::Vec3(1.5f, -2.0f, 3.25f))) && v.value.x() == 1.5f && v.value.y() == -2.0f &&
          v.value.z() == 3.25f && v.getValue() == iris::vec3ToValue(v.value),
          "Vec3: an Array of three numbers in, the same three floats out");
}

/// What today's Qt call makes of `in` for a row of `t` - the reference - and whether Qt converts it at all.
struct Expect { bool refused = false; iris::Value field; };
static Expect qtGives(iris::PropertyType t, const QVariant &in)
{
    Expect e;
    if (!in.isValid()) { e.refused = true; return e; }
    bool ok = false;
    switch (t) {
    case iris::PropertyType::Bool: e.field = iris::Value(in.toBool()); break;
    case iris::PropertyType::Int:
    case iris::PropertyType::List: { const int i = in.toInt(&ok); e.refused = !ok; e.field = iris::Value(i); break; }
    case iris::PropertyType::Float: { const float f = in.toFloat(&ok); e.refused = !ok; e.field = iris::Value(double(f)); break; }
    case iris::PropertyType::Color: {
        const QColor c = in.value<QColor>();
        e.refused = !c.isValid();
        e.field = iris::colourToValue(iris::toSrgbColor(c));
        break;
    }
    case iris::PropertyType::Vec3: {
        e.refused = in.metaType() != QMetaType::fromType<QVector3D>() && !in.canConvert<QVector3D>();
        const QVector3D q = in.value<QVector3D>();
        e.field = iris::vec3ToValue(iris::Vec3(q.x(), q.y(), q.z()));
        break;
    }
    case iris::PropertyType::Texture:
        e.field = in.typeId() == QMetaType::QVariantMap ? iris::Value(in.toMap().value("path").toString().toStdString())
                                                        : iris::Value(in.toString().toStdString());
        break;
    case iris::PropertyType::File: e.field = iris::Value(in.toString().toStdString()); break;
    case iris::PropertyType::None: e.refused = true; break;
    }
    return e;
}

static void testEdge()
{
    std::printf("\n-- (3) the same input through the edge gives what Qt gives; what Qt cannot convert is refused --\n");
    const std::vector<std::pair<const char *, QVariant>> inputs = {
        { "int 3", QVariant(3) },
        { "double 3.7", QVariant(3.7) },
        { "string \"3\"", QVariant(QStringLiteral("3")) },
        { "string \"abc\"", QVariant(QStringLiteral("abc")) },
        { "bool true", QVariant(true) },
        { "double 4294967295", QVariant(4294967295.0) },
        { "QColor rgb", QVariant(QColor(10, 20, 30)) },
        { "QColor hsv", QVariant(QColor::fromHsv(120, 200, 100)) },
        { "QVector3D", QVariant(QVector3D(1.0f, 2.0f, 3.0f)) },
        { "{path, guid}", QVariant(QVariantMap{{"path", QStringLiteral("/a/b.png")}, {"guid", QStringLiteral("g-7")}}) },
        { "invalid", QVariant() },
    };
    for (iris::PropertyType t : kKinds) {
        int checked = 0, refused = 0;
        for (const auto &in : inputs) {
            auto row = rowOf(t);
            const iris::Value before = row->getValue();
            const Expect e = qtGives(t, in.second);
            const iris::Value edge = qtstd::toRowValue(t, in.second);
            const bool set = row->setValue(edge);
            const std::string what = std::string(typeName(t)) + " <- " + in.first;
            if (e.refused) {
                QUIET(edge.isNull() && !set && row->getValue() == before, what + ": refused at the edge, the row unchanged");
                ++refused;
            } else {
                QUIET(set && row->getValue() == e.field, what + ": the row holds what Qt gives");
                if (t == iris::PropertyType::Texture && in.second.typeId() == QMetaType::QVariantMap)
                    QUIET(static_cast<iris::TextureProperty *>(row.get())->assetGuid == "g-7", what + ": ...and its guid");
            }
            ++checked;
        }
        CHECK(checked > 0, std::string("control: ") + typeName(t) + ": " + std::to_string(checked) + " edge inputs (" +
                               std::to_string(refused) + " refused)");
    }
    // Qt's conversions, pinned with the values measured on this Qt (2026-11-23).
    iris::IntProperty i;
    CHECK(i.setValue(qtstd::toRowValue(iris::PropertyType::Int, QVariant(3.7))) && i.value == 4, "Qt: 3.7 -> 4");
    CHECK(i.setValue(qtstd::toRowValue(iris::PropertyType::Int, QVariant(QStringLiteral("3")))) && i.value == 3, "Qt: \"3\" -> 3");
    CHECK(i.setValue(qtstd::toRowValue(iris::PropertyType::Int, QVariant(true))) && i.value == 1, "Qt: true -> 1");
    CHECK(i.setValue(qtstd::toRowValue(iris::PropertyType::Int, QVariant(4294967295.0))) && i.value == -1,
          "Qt: 4294967295.0 -> -1 (the mask's 32 bits)");
    CHECK(qtstd::toRowValue(iris::PropertyType::Int, QVariant(QStringLiteral("abc"))).isNull() &&
          qtstd::toRowValue(iris::PropertyType::Float, QVariant(QStringLiteral("abc"))).isNull(),
          "the ruling: \"abc\" is refused for an Int and a Float row (it was 0)");
    CHECK(qtstd::toRowValue(iris::PropertyType::Color, QVariant(QStringLiteral("not a colour"))).isNull() &&
          qtstd::toRowValue(iris::PropertyType::Vec3, QVariant(QStringLiteral("1,2,3"))).isNull(),
          "an invalid QColor and a non-vector are refused (never black, never zero)");
    // The edge back: toRowVariant gives each kind's old QVariant type.
    CHECK(qtstd::toRowVariant(iris::PropertyType::Float, iris::Value(0.5)).typeId() == QMetaType::Float &&
          qtstd::toRowVariant(iris::PropertyType::Int, iris::Value(3)).typeId() == QMetaType::Int &&
          qtstd::toRowVariant(iris::PropertyType::Color, iris::colourToValue(iris::SrgbColor(1, 2, 3))).value<QColor>() == QColor(1, 2, 3) &&
          qtstd::toRowVariant(iris::PropertyType::Vec3, iris::vec3ToValue(iris::Vec3(1, 2, 3))).value<QVector3D>() == QVector3D(1, 2, 3),
          "toRowVariant: Float a float, Int an int, Color a QColor, Vec3 a QVector3D");
}

static void testColourShape()
{
    std::printf("\n-- (4) the colour's Value shape over all 65,536 16-bit values --\n");
    int checked = 0;
    for (unsigned c = 0; c <= 0xffff; ++c) {
        const std::uint16_t v = std::uint16_t(c);
        const iris::SrgbColor col = iris::SrgbColor::fromRgba64(v, v, v, v);
        QUIET(iris::colourFromValue(iris::colourToValue(col)) == col, "the colour " + std::to_string(c) + " round-trips");
        ++checked;
    }
    CHECK(checked == 65536, "control: colours checked = " + std::to_string(checked));
    const iris::SrgbColor absent(1, 2, 3);
    CHECK(iris::colourFromValue(iris::Value(iris::Value::Object{}), absent) == absent &&
          iris::colourFromValue(iris::Value(), absent) == absent,
          "an empty Object and a Null read as `absent`");
    CHECK(iris::colourFromValue(iris::colourToValue(iris::SrgbColor()), absent) == iris::SrgbColor(0, 0, 0),
          "THE invalid colour's own shape (0, 0, 0, 1) reads as black, not as absent (RULED B's four numbers)");
}

int main()
{
    testMaterialRows();
    testRowClasses();
    testEdge();
    testColourShape();
    std::printf("document.property_rows: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
