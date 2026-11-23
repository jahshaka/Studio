// document.property_rows — THE STRICT DOCUMENT AND THE CONVERTING EDGE (NOQT-DOCUMENT-1 stage 4; the spec's RULED 20 and
// calls 5 and E; src/app/qtstdgui.h, irisgl/document/properties/property.h).
//
// THE LAWS:
// (1) ROUND TRIP: every row of every node class (SceneNode, Mesh, Light, Camera, Decal, ParticleSystem; a read-only
//     row refuses) and of a PbrMaterial (and every row class on its own) takes back the Value it gives:
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
//     The NODE's edge the same (RULED C: by the node's row; a rowless name by the kind it holds now; lightMask's
//     "abc" REFUSED, its 4294967295 every channel), and THE CAMERA's OVERRIDES by the key's type: Number toDouble
//     (finite), Toggle a Bool kept or toInt, Enum toInt, Stack an array; the strict setter takes RULED E's two cross-kind
//     inputs (an Int as a Number; a Bool or the Int 0 / 1 as a Toggle) and refuses every other.
// CONTROLS: rows exercised > 0 per row class and per node class; edge inputs > 0 per kind; colours checked = 65,536.
// CPU only: the document's own types, no engine, no display (Qt6::Gui is the edge under test and the reference).
#include "app/qtstd.h"
#include "app/qtstdgui.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/properties/property.h"
#include "irisgl/document/properties/valueshapes.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <QColor>
#include <QVariant>
#include <QVariantMap>
#include <QVector3D>

#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

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
    // A Null (THE invalid colour, "none") or an Object that is empty or carries any of r / g / b / a (the lead's C1).
    case iris::PropertyType::Color:
        return v.isNull() || (v.kind() == iris::Kind::Object &&
                              (v.asObject().empty() || v.find("r") || v.find("g") || v.find("b") || v.find("a")));
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
                  m.detail[0].weight, m.customPiecePixel.c_str(), m.customPieceVertex.c_str(),
                  int(m.textures.size()));
    std::string out = b;
    for (int l = 0; l < iris::PbrMaterial::kDetailLayers; ++l) {
        const auto &d = m.detail[l];
        std::snprintf(b, sizeof b, " [%d %g %g %g %g %g %g]", d.blend, d.offsetU, d.offsetV, d.scaleU, d.scaleV, d.weight,
                      d.normalWeight);
        out += b;
    }
    for (const std::string &row : iris::PbrMaterial::mapRowNames()) out += " " + std::to_string(m.addressFor(row));
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
        // A QColor-typed INVALID QColor is the none (a Null, as the old setter stored it); a QVariant Qt cannot
        // read as a colour is refused.
        e.refused = !c.isValid() && in.metaType() != QMetaType::fromType<QColor>();
        e.field = iris::colourRowValue(iris::toSrgbColor(c));
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
        { "QColor invalid (the none)", QVariant(QColor()) },
    };
    for (iris::PropertyType t : kKinds) {
        int checked = 0, refused = 0;
        for (const auto &in : inputs) {
            auto row = rowOf(t);
            const iris::Value before = row->getValue();
            const Expect e = qtGives(t, in.second);
            const std::optional<iris::Value> edge = qtstd::toRowValue(t, in.second);
            const bool set = edge && row->setValue(*edge);
            const std::string what = std::string(typeName(t)) + " <- " + in.first;
            if (e.refused) {
                QUIET(!edge && row->getValue() == before, what + ": refused at the edge, the row unchanged");
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
    const auto intOf = [](const QVariant &v) { return *qtstd::toRowValue(iris::PropertyType::Int, v); };
    CHECK(i.setValue(intOf(QVariant(3.7))) && i.value == 4, "Qt: 3.7 -> 4");
    CHECK(i.setValue(intOf(QVariant(QStringLiteral("3")))) && i.value == 3, "Qt: \"3\" -> 3");
    CHECK(i.setValue(intOf(QVariant(true))) && i.value == 1, "Qt: true -> 1");
    CHECK(i.setValue(intOf(QVariant(4294967295.0))) && i.value == -1,
          "Qt: 4294967295.0 -> -1 (the mask's 32 bits)");
    CHECK(!qtstd::toRowValue(iris::PropertyType::Int, QVariant(QStringLiteral("abc"))) &&
          !qtstd::toRowValue(iris::PropertyType::Float, QVariant(QStringLiteral("abc"))),
          "the ruling: \"abc\" is refused for an Int and a Float row (it was 0)");
    CHECK(!qtstd::toRowValue(iris::PropertyType::Color, QVariant(QStringLiteral("not a colour"))) &&
          !qtstd::toRowValue(iris::PropertyType::Vec3, QVariant(QStringLiteral("1,2,3"))),
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

/// Every row's Value of `n`, in order (what "nothing moved" compares).
static std::vector<iris::Value> nodeRows(iris::SceneNode &n)
{
    std::vector<iris::Value> out;
    const iris::PropertyRows rows = n.getProperties();
    for (const iris::Property *p : rows.view()) out.push_back(n.getPropertyValue(p->name));
    return out;
}

static void testNodeRows()
{
    std::printf("\n-- (1) and (2) for every row of every node class: its own Value back; every other kind refused --\n");
    const std::vector<std::pair<const char *, iris::SceneNodePtr>> nodes = {
        { "SceneNode", iris::SceneNode::create() },
        { "MeshNode", iris::MeshNode::create() },
        { "LightNode", iris::LightNode::create() },
        { "CameraNode", iris::CameraNode::create() },
        { "DecalNode", iris::DecalNode::create() },
        { "ParticleSystemNode", iris::ParticleSystemNode::create() },
    };
    for (const auto &entry : nodes) {
        iris::SceneNode &n = *entry.second;
        const std::string cls = entry.first;
        std::map<std::string, int> perType;
        int roundTrips = 0, refusals = 0;
        const iris::PropertyRows rows = n.getProperties();
        // Before any write (a write can move another field: a focal length switches the author mode).
        for (const iris::Property *prop : rows.view())
            QUIET(n.getPropertyValue(prop->name) == prop->getValue() &&
                      n.getPropertyValue(prop->name).kind() == prop->getValue().kind(),
                  cls + "." + prop->name + ": getPropertyValue is the row's Value, of the row's own KIND (an Int is no Number)");
        for (const iris::Property *prop : rows.view()) {
            ++perType[typeName(prop->type)];
            const iris::Value own = n.getPropertyValue(prop->name);
            if (prop->readOnly) {
                QUIET(!n.setPropertyValue(prop->name, own), cls + "." + prop->name + ": read-only, refused");
                continue;
            }
            // `preset` re-applies a recipe (other rows move by design): its own row is what round-trips.
            QUIET(n.setPropertyValue(prop->name, own) && n.getPropertyValue(prop->name) == own,
                  cls + "." + prop->name + ": its own Value round-trips");
            ++roundTrips;
            for (const auto &k : everyKind()) {
                if (takes(prop->type, k.second)) continue;
                const std::vector<iris::Value> before = nodeRows(n);
                QUIET(!n.setPropertyValue(prop->name, k.second) && nodeRows(n) == before,
                      cls + "." + prop->name + " (" + typeName(prop->type) + ") refuses " + k.first + ", nothing moved");
                ++refusals;
            }
        }
        CHECK(roundTrips > 0 && refusals > 0, "control: " + cls + ": " + std::to_string(roundTrips) + " rows round-trip, " +
                                                  std::to_string(refusals) + " wrong kinds refused");
    }
    auto light = iris::LightNode::create();
    CHECK(light->setPropertyValue("intensity", iris::Value(3)) && light->intensity == 3.0f,
          "RULED E (1): a node's Float row takes an Int as its number");
    CHECK(!light->setPropertyValue("lightType", iris::Value(2.0)),
          "RULED E: an integral Number into a node's Int row is REFUSED");
    CHECK(light->setPropertyValue("iesNormalisation", iris::Value(0.5)) && light->iesNormalisation == 0.5f &&
          !light->setPropertyValue("iesNormalisation", iris::Value("0.5")),
          "a rowless field takes its own kind and refuses another");
    CHECK(light->getPropertyValue("noSuchName").isNull() && !light->setPropertyValue("noSuchName", iris::Value(1)),
          "an unknown name reads Null and is refused");
}

static void testNodeEdge()
{
    std::printf("\n-- (3) the node's edge: by the row (RULED C), a rowless name by its current kind --\n");
    auto light = iris::LightNode::create();
    const auto set = [&](const char *name, const QVariant &v) {
        const auto value = qtstd::toNodeValue(*light, name, v);
        return value && light->setPropertyValue(name, *value);
    };
    CHECK(set("intensity", QVariant(QStringLiteral("3"))) && light->intensity == 3.0f,
          "a Float row: \"3\" -> 3 (Qt's toFloat)");
    CHECK(!qtstd::toNodeValue(*light, "intensity", QVariant(QStringLiteral("abc"))), "...\"abc\" refused at the edge");
    CHECK(set("lightMask", QVariant(4294967295.0)) && light->getPropertyValue("lightMask") == iris::Value(-1),
          "lightMask: 4294967295.0 -> every channel (-1, the same 32 bits)");
    CHECK(!qtstd::toNodeValue(*light, "lightMask", QVariant(QStringLiteral("abc"))),
          "lightMask: \"abc\" REFUSED (the ruling: never a mask of 0)");
    CHECK(set("lightColor", QVariant(QColor(10, 20, 30))) && light->color == iris::SrgbColor(10, 20, 30),
          "a Color row: a QColor -> its colour");
    CHECK(!qtstd::toNodeValue(*light, "lightColor", QVariant(QStringLiteral("not a colour"))),
          "...an invalid QColor refused");
    CHECK(set("iesNormalisation", QVariant(QStringLiteral("2.5"))) && light->iesNormalisation == 2.5f,
          "a ROWLESS name converts by its current kind (a Number: toFloat)");
    CHECK(set("iesProfile", QVariant(7)) && light->iesProfileGuid == QStringLiteral("7"),
          "...a rowless String takes toString");
    CHECK(set("position", QVariant(QVector3D(1.0f, 2.0f, 3.0f))) && light->getLocalPos().y() == 2.0f,
          "a Vec3 row: a QVector3D -> its vector");
    CHECK(!qtstd::toNodeValue(*light, "position", QVariant(QStringLiteral("1,2,3"))), "...a non-vector refused");
    CHECK(!qtstd::toNodeValue(*light, "noSuchName", QVariant(1)), "an unknown name: refused at the edge");
    CHECK(qtstd::nodeVariant(*light, "intensity").typeId() == QMetaType::Float &&
          qtstd::nodeVariant(*light, "position").typeId() == QMetaType::QVector3D &&
          qtstd::nodeVariant(*light, "lightColor").typeId() == QMetaType::QColor &&
          qtstd::nodeVariant(*light, "iesProfile").typeId() == QMetaType::QString &&
          !qtstd::nodeVariant(*light, "noSuchName").isValid(),
          "nodeVariant: the QVariant types the node handed out before");
}

static void testCameraOverrides()
{
    std::printf("\n-- the camera's overrides: the edge by the key's type; the strict setter; RULED E (1) and (2) --\n");
    auto cam = iris::CameraNode::create();
    const auto post = [&](const char *key, const QVariant &v) { return qtstd::toPostValue(key, v); };
    CHECK(post("bloomThreshold", QVariant(QStringLiteral("0.5"))) == std::optional<iris::Value>(iris::Value(0.5)),
          "Number: \"0.5\" -> 0.5 (toDouble)");
    CHECK(!post("bloomThreshold", QVariant(QStringLiteral("abc"))) &&
          !post("bloomThreshold", QVariant(std::numeric_limits<double>::quiet_NaN())),
          "Number: \"abc\" and NaN refused at the edge");
    CHECK(post("bloom", QVariant(true)) == std::optional<iris::Value>(iris::Value(true)) &&
          post("bloom", QVariant(1)) == std::optional<iris::Value>(iris::Value(1)),
          "Toggle: a Bool kept, an int its Int");
    CHECK(post("bloom", QVariant()) == std::optional<iris::Value>(iris::Value()), "an invalid QVariant is the CLEAR (Null)");
    CHECK(!post("noSuchKey", QVariant(1)), "an unknown key: refused");
    CHECK(cam->setPostOverride("bloomThreshold", iris::Value(1)) &&
          cam->postOverride("bloomThreshold") == iris::Value(1.0),
          "RULED E (1): a Number key takes an Int as its number");
    CHECK(cam->setPostOverride("bloom", iris::Value(true)) && cam->postOverride("bloom") == iris::Value(1) &&
          cam->setPostOverride("bloom", iris::Value(0)) && cam->postOverride("bloom") == iris::Value(0),
          "RULED E (2): a Toggle key takes a Bool and the Int 0 / 1");
    const iris::Value before = cam->postOverrides;
    CHECK(!cam->setPostOverride("bloom", iris::Value(2)) && !cam->setPostOverride("bloom", iris::Value("on")) &&
          !cam->setPostOverride("bloomThreshold", iris::Value("0.5")) && !cam->setPostOverride("smaa", iris::Value(2)) &&
          !cam->setPostOverride("smaa", iris::Value(-1.0)) && !cam->setPostOverride("looks", iris::Value(1)) &&
          cam->postOverrides == before,
          "every other kind refused (a Toggle's 2, a string, an SMAA preset, an integral Number into the Enum, a "
          "non-array stack), nothing moved");
    const QVariantList stack{ QVariantMap{ { "id", QStringLiteral("desaturate") } } };
    const auto stackValue = post("looks", QVariant(stack));
    CHECK(stackValue && cam->setPostOverride("looks", *stackValue) &&
          cam->postOverrideStack("looks").asArray().size() == 1 &&
          cam->postOverrideStack("looks").asArray()[0]["id"] == iris::Value("desaturate"),
          "Stack: a QVariantList through the edge, normalised by the one validator");
    CHECK(cam->setPropertyValue("postFx.bloom", iris::Value()) && !cam->hasPostOverride("bloom") &&
          cam->getPropertyValue("postFx.bloom").isNull(),
          "a Null through setPropertyValue clears the override (inherit)");
}

static void testUnsetColour()
{
    std::printf("\n-- THE INVALID COLOUR IS A NULL (the lead's W1): no fade stays no fade --\n");
    auto ps = iris::ParticleSystemNode::create();
    // A new emitter's "no fade" is the VALID transparent black (particlesystemnode.cpp's ctor: (0, 0, 0, 0), whose sum
    // the mirror's haveFade reads as zero): it round-trips as that colour.
    const iris::SrgbColor zero(0, 0, 0, 0);
    CHECK(ps->colourFade1 == zero, "control: a new emitter's fade is (0, 0, 0, 0): no fade");
    CHECK(ps->setPropertyValue("colourFade1", ps->getPropertyValue("colourFade1")) && ps->colourFade1 == zero &&
          ps->colourFade1.isValid(), "get -> set: the default stays (0, 0, 0, 0), valid (no fade)");
    // THE INVALID colour (the other "none" the mirror reads, by isValid()): a Null, kept a Null everywhere.
    CHECK(ps->setPropertyValue("colourFade1", iris::Value()) && !ps->colourFade1.isValid(),
          "a Null sets THE invalid colour");
    const iris::Value none = ps->getPropertyValue("colourFade1");
    CHECK(none.isNull(), "getPropertyValue: an invalid fade is a Null, never black");
    CHECK(ps->setPropertyValue("colourFade1", none) && !ps->colourFade1.isValid(),
          "get -> set: the invalid fade stays invalid (the mirror's isValid() read: no fade)");
    const QVariant shown = qtstd::nodeVariant(*ps, "colourFade1");
    CHECK(shown.typeId() == QMetaType::QColor && !shown.value<QColor>().isValid(),
          "the edge out: the invalid QColor the node handed out before");
    const auto back = qtstd::toNodeValue(*ps, "colourFade1", shown);
    CHECK(back && back->isNull() && ps->setPropertyValue("colourFade1", *back) && !ps->colourFade1.isValid(),
          "the edge back in: the invalid QColor is the none, and the fade stays invalid");
    CHECK(ps->setPropertyValue("colourFade1", iris::colourRowValue(iris::SrgbColor(128, 128, 128, 255))) &&
          ps->colourFade1.isValid() && ps->setPropertyValue("colourFade1", iris::Value()) && !ps->colourFade1.isValid(),
          "a set fade is cleared by a Null");
    auto mat = iris::PbrMaterial::create();
    const iris::SrgbColor base = mat->baseColor;
    CHECK(!mat->setValue("baseColor", iris::Material::textureRef("/a.png", "g")) && mat->baseColor == base,
          "C1: a {guid, path} Object into a colour row is REFUSED (it carries none of r / g / b / a)");
}

int main()
{
    testUnsetColour();
    testMaterialRows();
    testNodeRows();
    testNodeEdge();
    testCameraOverrides();
    testRowClasses();
    testEdge();
    testColourShape();
    if (failures) std::printf("document.property_rows: FAILED (%d)\n", failures);
    else std::printf("document.property_rows: PASSED\n");
    return failures ? 1 : 0;
}
