// THE APP's ONE Qt-CONVERSION HEADER (NOQT-CORE-1, V2 plan 0.12): where the editor converts between Qt's types and the
// standard types of the layers below it (paths, text, bytes, and what the document's own specs add). Nothing below src/app
// includes it - irisgl/ never does (tests/hygiene/no_qt_below.py holds that). Conversions only: no logic lives here.
// QtCore only: a QtGui type (a colour, a vector, a property row's QVariant) is src/app/qtstdgui.h's.
#pragma once

#include "irisgl/core/files.h"
#include "irisgl/core/value.h"
#include "irisgl/document/properties/valueshapes.h"
#include "irisgl/document/srgbcolor.h"

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qtstd {

/// A QString path <-> a std::filesystem::path, the same on every platform: through UTF-8 and iris::files' u8path door
/// (a narrow std::string is not UTF-8 on Windows; QString::toStdString IS UTF-8).
inline std::filesystem::path toPath(const QString &p) { return iris::files::path(p.toStdString()); }
inline QString fromPath(const std::filesystem::path &p) { return QString::fromStdString(iris::files::utf8(p)); }

/// UTF-8 text below the editor -> a QString.
inline QString toQString(std::string_view utf8) { return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size())); }

/// A document's list of UTF-8 strings <-> the editor's QStringList (the same order).
inline QStringList toQStringList(const std::vector<std::string> &list)
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(list.size()));
    for (const std::string &s : list) out.append(QString::fromStdString(s));
    return out;
}
inline std::vector<std::string> toStdStrings(const QStringList &list)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(list.size()));
    for (const QString &s : list) out.push_back(s.toStdString());
    return out;
}

/// The editor's guid -> guid map (a paste's, a duplicate's) as the document's.
inline std::unordered_map<std::string, std::string> toStdStringMap(const QHash<QString, QString> &map)
{
    std::unordered_map<std::string, std::string> out;
    out.reserve(static_cast<std::size_t>(map.size()));
    for (auto it = map.cbegin(); it != map.cend(); ++it) out.emplace(it.key().toStdString(), it.value().toStdString());
    return out;
}

/// A QByteArray's bytes as a view (valid while the array lives and is not changed).
inline std::string_view toView(const QByteArray &bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// ---- iris::Value <-> QJsonValue (NOQT-DOCUMENT-1 stage 4) ------------------------------------------------------------
// AN Int STAYS AN Int AND A Number A Number both ways: a QJsonValue keeps an integer as a qint64 (its toVariant() says
// which it holds), so 2^53 + 1 crosses exactly. An Object crosses into a QJsonObject, which orders its keys itself (the
// file's bytes are QJsonDocument's, as before); a QJsonObject crosses back in its own (sorted) order. Bytes are written
// as core/json.h writes them, {"$bytes": "<base64>"}.

inline iris::Value toValue(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Bool:   return iris::Value(v.toBool());
    case QJsonValue::Double: {
        const QVariant n = v.toVariant();
        if (n.metaType().id() == QMetaType::LongLong) return iris::Value(static_cast<std::int64_t>(n.toLongLong()));
        return iris::Value(v.toDouble());
    }
    case QJsonValue::String: return iris::Value(v.toString().toStdString());
    case QJsonValue::Array: {
        iris::Value::Array out;
        const QJsonArray a = v.toArray();
        out.reserve(static_cast<std::size_t>(a.size()));
        for (const QJsonValue &e : a) out.push_back(toValue(e));
        return iris::Value(std::move(out));
    }
    case QJsonValue::Object: {
        iris::Value::Object out;
        const QJsonObject o = v.toObject();
        out.reserve(static_cast<std::size_t>(o.size()));
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) out.emplace_back(it.key().toStdString(), toValue(it.value()));
        return iris::Value(std::move(out));
    }
    case QJsonValue::Null:
    case QJsonValue::Undefined: break;
    }
    return iris::Value();
}

inline QJsonValue toQJsonValue(const iris::Value &v)
{
    switch (v.kind()) {
    case iris::Kind::Null:   return QJsonValue(QJsonValue::Null);
    case iris::Kind::Bool:   return QJsonValue(v.asBool());
    case iris::Kind::Int:    return QJsonValue(static_cast<qint64>(v.asInt()));
    case iris::Kind::Number: return QJsonValue(v.asNumber());
    case iris::Kind::String: return QJsonValue(toQString(v.asString()));
    case iris::Kind::Array: {
        QJsonArray out;
        for (const iris::Value &e : v.asArray()) out.append(toQJsonValue(e));
        return out;
    }
    case iris::Kind::Object: {
        QJsonObject out;
        for (const iris::Value::Member &m : v.asObject()) out.insert(toQString(m.first), toQJsonValue(m.second));
        return out;
    }
    case iris::Kind::Bytes: {
        const iris::Value::Bytes &b = v.asBytes();
        const QByteArray raw(reinterpret_cast<const char *>(b.data()), static_cast<qsizetype>(b.size()));
        return QJsonObject{{QStringLiteral("$bytes"), QString::fromLatin1(raw.toBase64())}};
    }
    }
    return QJsonValue(QJsonValue::Null);
}

/// The two Object shorthands (a document member that is an Object <-> the QJsonObject an editor file or panel builds):
/// any other kind reads as the empty QJsonObject, as QJsonValue::toObject() does.
inline QJsonObject toQJsonObject(const iris::Value &v) { return toQJsonValue(v).toObject(); }
inline iris::Value fromQJsonObject(const QJsonObject &o) { return toValue(QJsonValue(o)); }

// ---- iris::Value <-> QVariant of the core kinds --------------------------------------------------------------------
// Null <-> an invalid QVariant; Bool <-> bool; Int <-> qlonglong (any integral QVariant in); Number <-> double (a float
// widened); String <-> QString; Array <-> QVariantList (a QStringList in); Object <-> QVariantMap (a QVariantHash in);
// a QJsonValue / QJsonObject / QJsonArray through toValue. Anything else (a QtGui type: qtstdgui.h) reads as Null.

inline iris::Value fromVariant(const QVariant &v);

inline QVariant toVariant(const iris::Value &v)
{
    switch (v.kind()) {
    case iris::Kind::Null:   return QVariant();
    case iris::Kind::Bool:   return QVariant(v.asBool());
    case iris::Kind::Int:    return QVariant(static_cast<qlonglong>(v.asInt()));
    case iris::Kind::Number: return QVariant(v.asNumber());
    case iris::Kind::String: return QVariant(toQString(v.asString()));
    case iris::Kind::Array: {
        QVariantList out;
        for (const iris::Value &e : v.asArray()) out.append(toVariant(e));
        return out;
    }
    case iris::Kind::Object: {
        QVariantMap out;
        for (const iris::Value::Member &m : v.asObject()) out.insert(toQString(m.first), toVariant(m.second));
        return out;
    }
    case iris::Kind::Bytes: {
        const iris::Value::Bytes &b = v.asBytes();
        return QVariant(QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<qsizetype>(b.size())));
    }
    }
    return QVariant();
}

/// A Value Object as the QVariantMap a widget or a verb reads (an empty map for any other kind).
inline QVariantMap toVariantMap(const iris::Value &v)
{
    return v.kind() == iris::Kind::Object ? toVariant(v).toMap() : QVariantMap();
}

inline iris::Value fromVariant(const QVariant &v)
{
    switch (v.metaType().id()) {
    case QMetaType::Bool:      return iris::Value(v.toBool());
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Long:
    case QMetaType::LongLong:  return iris::Value(static_cast<std::int64_t>(v.toLongLong()));
    case QMetaType::ULong:
    case QMetaType::ULongLong: return iris::Value(static_cast<std::int64_t>(v.toULongLong()));
    case QMetaType::Float:
    case QMetaType::Double:    return iris::Value(v.toDouble());
    case QMetaType::QString:   return iris::Value(v.toString().toStdString());
    case QMetaType::QByteArray: {
        const QByteArray b = v.toByteArray();
        return iris::Value::bytes(iris::Value::Bytes(b.begin(), b.end()));
    }
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        iris::Value::Array out;
        const QVariantList l = v.toList();
        out.reserve(static_cast<std::size_t>(l.size()));
        for (const QVariant &e : l) out.push_back(fromVariant(e));
        return iris::Value(std::move(out));
    }
    case QMetaType::QVariantMap:
    case QMetaType::QVariantHash: {
        iris::Value::Object out;
        const QVariantMap m = v.toMap();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) out.emplace_back(it.key().toStdString(), fromVariant(it.value()));
        return iris::Value(std::move(out));
    }
    case QMetaType::QJsonValue:  return toValue(v.toJsonValue());
    case QMetaType::QJsonObject: return toValue(QJsonValue(v.toJsonObject()));
    case QMetaType::QJsonArray:  return toValue(QJsonValue(v.toJsonArray()));
    default: break;
    }
    return iris::Value();
}

// ---- THE COLOUR's QJsonObject CODEC (RULED A): one shape, one clamp - valueshapes.h's, through toQJsonValue ----------

/// `c` as {r, g, b, a}, each the F accessor widened (an invalid colour writes its own channels: SrgbColor() 0, 0, 0, 1).
inline QJsonObject colorToJson(const iris::SrgbColor &c) { return toQJsonValue(iris::colourToValue(c)).toObject(); }

/// The colour an {r, g, b, a} object holds (channels clamped to [0, 1]; `a` absent = opaque); an EMPTY object is `absent`.
inline iris::SrgbColor colorFromJson(const QJsonObject &o, const iris::SrgbColor &absent = iris::SrgbColor())
{
    return iris::colourFromValue(toValue(QJsonValue(o)), absent);
}

}   // namespace qtstd
