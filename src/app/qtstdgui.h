// THE APP's QtGui CONVERSION HEADER (NOQT-DOCUMENT-1 stage 4, the spec's RULED call 1): src/app/qtstd.h's QtGui half -
// a colour, a vector and the QVariant a property row's widget, verb or command carries. A QtGui type never sits in a
// header a QtCore-only target includes, so it is its own header. Nothing below src/app includes it. Conversions only.
#pragma once

#include "app/qtstd.h"
#include "irisgl/core/math/mat3.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/core/value.h"
#include "irisgl/document/properties/property.h"
#include "irisgl/document/properties/valueshapes.h"
#include "irisgl/document/qt/colour.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <QColor>
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector2D>
#include <QVector4D>
#include <QJsonArray>
#include <QJsonValue>
#include <QVariant>
#include <QVariantMap>
#include <QVector3D>

#include <cmath>
#include <optional>
#include <string_view>

namespace qtstd {

/// A QColor as a colour PROPERTY's Value (valueshapes.h colourRowValue): its Object shape; THE invalid QColor a Null.
inline iris::Value colourValue(const QColor &c) { return iris::colourRowValue(iris::toSrgbColor(c)); }
/// The QColor a colour Value holds (a Null, an empty object or a non-object: the invalid QColor).
inline QColor toQColor(const iris::Value &v) { return iris::toQColor(iris::colourFromValue(v)); }

/// A QVector3D as an Array of three Numbers, and back (anything else: the zero vector, as QVariant::value<QVector3D>).
inline iris::Value vec3Value(const QVector3D &v) { return iris::vec3ToValue(iris::Vec3(v.x(), v.y(), v.z())); }
inline QVector3D toQVector3D(const iris::Value &v)
{
    iris::Vec3 out;
    iris::vec3FromValue(v, out);
    return QVector3D(out.x(), out.y(), out.z());
}

/// THE EDITOR's ONE CONVERSION OF A QVariant TO A PROPERTY ROW's KIND (the spec's RULED 20): Qt's own calls, the ones the
/// rows' setters made before the document took Values - Bool `toBool()`; Int and List `toInt(&ok)`; Float `toFloat(&ok)`
/// widened; Color `value<QColor>()`; Vec3 `value<QVector3D>()`; Texture a QVariantMap's {path, guid}, else `toString()`;
/// File `toString()`. What Qt CONVERTS stays converted exactly as before (3.7 -> 4, "3" -> 3, true -> 1, 4294967295.0 ->
/// -1); WHAT IT CANNOT CONVERT IS REFUSED (the lead's ruling, 2026-11-23: "a value is refused by name, never silently
/// moved"): a number that does not read (toInt / toFloat's ok false), a QVariant that is no colour, a QVariant that is
/// not a vector, and an INVALID QVariant of any kind answer std::nullopt (a verb reads it and fails by name first).
/// (The spec wrote `iris::Value toRowValue(...)`; it returns an optional because a colour row's NULL is a value - THE
/// invalid colour, "none": an unset particle fade - so a Null cannot also mean "refused": the lead's W1, 2026-11-23.)
/// A QColor-typed QVariant holding the INVALID QColor is that none (a Null), as the old setter stored it.
inline std::optional<iris::Value> toRowValue(iris::PropertyType type, const QVariant &v)
{
    if (!v.isValid()) return std::nullopt;
    bool ok = false;
    switch (type) {
    case iris::PropertyType::Bool:
        return iris::Value(v.toBool());
    case iris::PropertyType::Int:
    case iris::PropertyType::List: {
        const int i = v.toInt(&ok);
        if (!ok) return std::nullopt;
        return iris::Value(i);
    }
    case iris::PropertyType::Float: {
        const float f = v.toFloat(&ok);
        if (!ok) return std::nullopt;
        return iris::Value(double(f));
    }
    case iris::PropertyType::Color: {
        const QColor c = v.value<QColor>();
        if (!c.isValid() && v.metaType() != QMetaType::fromType<QColor>()) return std::nullopt;
        return colourValue(c);
    }
    case iris::PropertyType::Vec3:
        if (v.metaType() != QMetaType::fromType<QVector3D>() && !v.canConvert<QVector3D>()) return std::nullopt;
        return vec3Value(v.value<QVector3D>());
    case iris::PropertyType::Texture:
        if (v.typeId() == QMetaType::QVariantMap) {
            const QVariantMap ref = v.toMap();
            return iris::Value(iris::Value::Object{
                {"guid", iris::Value(ref.value(QStringLiteral("guid")).toString().toStdString())},
                {"path", iris::Value(ref.value(QStringLiteral("path")).toString().toStdString())}});
        }
        return iris::Value(v.toString().toStdString());
    case iris::PropertyType::File:
        return iris::Value(v.toString().toStdString());
    case iris::PropertyType::None:
        break;
    }
    return std::nullopt;
}

/// A row's Value as the QVariant its kind was before (Bool bool, Int / List int, Float float, Color QColor, Vec3
/// QVector3D, Texture / File QString): for a reader that still speaks QVariant (a verb, an undo command, a widget).
inline QVariant toRowVariant(iris::PropertyType type, const iris::Value &v)
{
    switch (type) {
    case iris::PropertyType::Bool:    return v.kind() == iris::Kind::Bool ? QVariant(v.asBool()) : QVariant();
    case iris::PropertyType::Int:
    case iris::PropertyType::List:    return v.kind() == iris::Kind::Int ? QVariant(static_cast<int>(v.asInt())) : QVariant();
    case iris::PropertyType::Float: {
        double d = 0.0;
        return iris::numberOf(v, d) ? QVariant::fromValue(float(d)) : QVariant();
    }
    case iris::PropertyType::Color:   return QVariant::fromValue(toQColor(v));
    case iris::PropertyType::Vec3:    return QVariant::fromValue(toQVector3D(v));
    case iris::PropertyType::Texture:
    case iris::PropertyType::File:    return v.kind() == iris::Kind::String ? QVariant(toQString(v.asString())) : QVariant();
    case iris::PropertyType::None:    break;
    }
    return QVariant();
}


// ---- THE NODE's EDGE (the spec's RULED C): a node property's kind is the node's ROW of that name; the camera's
// `postFx.<key>` is cameraPostKey(key)'s type; a name with no row (decalNormal, iesProfile, focusTarget, ...) is the
// kind of the Value it holds now - as NodeApi converted by the current QVariant's type before (the lead's ruling,
// 2026-11-23). REFUSED = std::nullopt (a postFx Null is the CLEAR, a value, not a refusal).

/// The type of `node`'s row called `name`; None when it has none.
inline iris::PropertyType rowType(iris::SceneNode &node, std::string_view name)
{
    const iris::PropertyRows rows = node.getProperties();
    for (const iris::Property *p : rows.view())
        if (p->name == name) return p->type;
    return iris::PropertyType::None;
}

/// A scalar Value's kind as the row type that converts it (a rowless name's).
inline iris::PropertyType typeOfKind(const iris::Value &v)
{
    switch (v.kind()) {
    case iris::Kind::Bool:   return iris::PropertyType::Bool;
    case iris::Kind::Int:    return iris::PropertyType::Int;
    case iris::Kind::Number: return iris::PropertyType::Float;
    case iris::Kind::String: return iris::PropertyType::File;
    default: break;
    }
    return iris::PropertyType::None;
}

/// THE CAMERA's OVERRIDE EDGE, by the key's type (the conversions CameraNode::setPostOverride made on a QVariant,
/// moved here with their refusals): Number `toDouble(&ok)` and finite; Toggle a Bool kept, else `toInt(&ok)`; Enum
/// `toInt(&ok)`; Stack a QJsonArray, a QVariantList or a QJsonValue array. An invalid or null QVariant is the CLEAR
/// (a Null Value). An unknown key or a value its type cannot read: nullopt.
inline std::optional<iris::Value> toPostValue(std::string_view key, const QVariant &v)
{
    const iris::CameraPostKey *k = iris::cameraPostKey(key);
    if (!k) return std::nullopt;
    if (!v.isValid() || v.isNull()) return iris::Value();
    bool ok = false;
    switch (k->type) {
    case iris::CameraPostKeyType::Number: {
        const double d = v.toDouble(&ok);
        if (!ok || !std::isfinite(d)) return std::nullopt;
        return iris::Value(d);
    }
    case iris::CameraPostKeyType::Toggle: {
        if (v.typeId() == QMetaType::Bool) return iris::Value(v.toBool());
        const int i = v.toInt(&ok);
        if (!ok) return std::nullopt;
        return iris::Value(i);
    }
    case iris::CameraPostKeyType::Enum: {
        const int i = v.toInt(&ok);
        if (!ok) return std::nullopt;
        return iris::Value(i);
    }
    case iris::CameraPostKeyType::Stack:
        if (v.metaType().id() == QMetaType::QJsonArray || v.metaType().id() == QMetaType::QVariantList)
            return fromVariant(v);
        if (v.metaType().id() == QMetaType::QJsonValue && v.toJsonValue().isArray()) return fromVariant(v);
        return std::nullopt;
    }
    return std::nullopt;
}

/// A QVariant for `node`'s property `name`, converted to its kind (above). nullopt = refused.
inline std::optional<iris::Value> toNodeValue(iris::SceneNode &node, std::string_view name, const QVariant &v)
{
    if (name.substr(0, 7) == "postFx.") return toPostValue(name.substr(7), v);
    iris::PropertyType type = rowType(node, name);
    if (type == iris::PropertyType::None) type = typeOfKind(node.getPropertyValue(name));
    if (type == iris::PropertyType::None) return std::nullopt;
    return toRowValue(type, v);
}

/// `value` (of `node`'s property `name`) as the QVariant the node handed out before: a vector a QVector3D, a colour
/// a QColor, a float a float, an int an int, a bool, a QString; a postFx Number a double, Toggle / Enum an int, the
/// Stack a QVariantList; a Null an invalid QVariant.
inline QVariant toNodeVariant(iris::SceneNode &node, std::string_view name, const iris::Value &value)
{
    if (name.substr(0, 7) == "postFx.") {
        if (value.isNull()) return QVariant();   // inherited
        const iris::CameraPostKey *k = iris::cameraPostKey(name.substr(7));
        if (k && k->type == iris::CameraPostKeyType::Number) return QVariant(value.asNumber());
        if (k && k->type == iris::CameraPostKeyType::Stack) return toVariant(value);
        return QVariant(static_cast<int>(value.asNumber()));
    }
    // A colour row's Null is THE invalid colour (an invalid QColor, as the node handed out before); any other Null is
    // an unknown name: an invalid QVariant.
    iris::PropertyType type = rowType(node, name);
    if (type == iris::PropertyType::None) type = typeOfKind(value);
    return toRowVariant(type, value);
}

/// The node's property `name` now, as toNodeVariant gives it.
inline QVariant nodeVariant(iris::SceneNode &node, std::string_view name)
{
    return toNodeVariant(node, name, node.getPropertyValue(name));
}


// ---- iris <-> QtGui MATHS (NOQT-DOCUMENT-1 stage 4, walk 11: moved here whole from irisgl/document/qt/qtinterop.h, which
// is deleted; the document no longer converts: only the editor, where a Qt type genuinely arrives or departs - a spin
// box, a gizmo, a QVariant / QJSValue bridge, a serializer). Member-for-member copies, exact by construction. ONE
// ASYMMETRY: fromQt(QMatrix4x4) cannot recover Qt's private flag bits, so the result is flagged General, as any iris::Mat4
// built from raw floats.
// ---- iris -> Qt --------------------------------------------------------------
inline QVector2D toQt(const iris::Vec2 &v) { return QVector2D(v.x(), v.y()); }
inline QVector3D toQt(const iris::Vec3 &v) { return QVector3D(v.x(), v.y(), v.z()); }
inline QVector4D toQt(const iris::Vec4 &v) { return QVector4D(v.x(), v.y(), v.z(), v.w()); }
inline QQuaternion toQt(const iris::Quat &q) { return QQuaternion(q.scalar(), q.x(), q.y(), q.z()); }

inline QMatrix3x3 toQt(const iris::Mat3 &m)
{
    QMatrix3x3 r;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r(row, col) = m(row, col);
    return r;
}

inline QMatrix4x4 toQt(const iris::Mat4 &m)
{
    // The 16-argument constructor takes values in ROW-MAJOR order.
    return QMatrix4x4(m(0, 0), m(0, 1), m(0, 2), m(0, 3),
                      m(1, 0), m(1, 1), m(1, 2), m(1, 3),
                      m(2, 0), m(2, 1), m(2, 2), m(2, 3),
                      m(3, 0), m(3, 1), m(3, 2), m(3, 3));
}

// ---- Qt -> iris --------------------------------------------------------------
inline iris::Vec2 fromQt(const QVector2D &v) { return iris::Vec2(v.x(), v.y()); }
inline iris::Vec3 fromQt(const QVector3D &v) { return iris::Vec3(v.x(), v.y(), v.z()); }
inline iris::Vec4 fromQt(const QVector4D &v) { return iris::Vec4(v.x(), v.y(), v.z(), v.w()); }
inline iris::Quat fromQt(const QQuaternion &q) { return iris::Quat(q.scalar(), q.x(), q.y(), q.z()); }

inline iris::Mat3 fromQt(const QMatrix3x3 &m)
{
    iris::Mat3 r(iris::Mat3::Uninitialized);
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            r(row, col) = m(row, col);
    return r;
}

inline iris::Mat4 fromQt(const QMatrix4x4 &m)
{
    return iris::Mat4(m(0, 0), m(0, 1), m(0, 2), m(0, 3),
                m(1, 0), m(1, 1), m(1, 2), m(1, 3),
                m(2, 0), m(2, 1), m(2, 2), m(2, 3),
                m(3, 0), m(3, 1), m(3, 2), m(3, 3));
}

}   // namespace qtstd
