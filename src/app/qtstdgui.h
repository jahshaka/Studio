// THE APP's QtGui CONVERSION HEADER (NOQT-DOCUMENT-1 stage 4, the spec's RULED call 1): src/app/qtstd.h's QtGui half -
// a colour, a vector and the QVariant a property row's widget, verb or command carries. A QtGui type never sits in a
// header a QtCore-only target includes, so it is its own header. Nothing below src/app includes it. Conversions only.
#pragma once

#include "app/qtstd.h"
#include "irisgl/core/value.h"
#include "irisgl/document/properties/property.h"
#include "irisgl/document/properties/valueshapes.h"
#include "irisgl/document/qt/colour.h"

#include <QColor>
#include <QVariant>
#include <QVariantMap>
#include <QVector3D>

namespace qtstd {

/// A QColor as the colour's Value shape (valueshapes.h), through the document's QColor adapter.
inline iris::Value colourValue(const QColor &c) { return iris::colourToValue(iris::toSrgbColor(c)); }
/// The QColor a colour Value holds (an empty object, a Null or a non-object: the invalid QColor).
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
/// moved"): a number that does not read (toInt / toFloat's ok false), an invalid QColor, a QVariant that is not a
/// vector, and an INVALID QVariant of any kind answer a NULL Value - the kind no row holds, so the document's strict
/// setter refuses it, and a verb reads isNull() to fail by name first.
inline iris::Value toRowValue(iris::PropertyType type, const QVariant &v)
{
    if (!v.isValid()) return iris::Value();
    bool ok = false;
    switch (type) {
    case iris::PropertyType::Bool:
        return iris::Value(v.toBool());
    case iris::PropertyType::Int:
    case iris::PropertyType::List: {
        const int i = v.toInt(&ok);
        return ok ? iris::Value(i) : iris::Value();
    }
    case iris::PropertyType::Float: {
        const float f = v.toFloat(&ok);
        return ok ? iris::Value(double(f)) : iris::Value();
    }
    case iris::PropertyType::Color: {
        const QColor c = v.value<QColor>();
        return c.isValid() ? colourValue(c) : iris::Value();
    }
    case iris::PropertyType::Vec3:
        if (v.metaType() != QMetaType::fromType<QVector3D>() && !v.canConvert<QVector3D>()) return iris::Value();
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
    return iris::Value();
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

}   // namespace qtstd
