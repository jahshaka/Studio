/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/assetdrag.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

namespace AssetDrag {

const char *format()
{
    return "application/x-qabstractitemmodeldatalist";
}

QMimeData *mimeFor(int type, const QString &name, const QString &mesh, const QString &guid)
{
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    QMap<int, QVariant> roleDataMap;
    roleDataMap[TypeSlot] = QVariant(type);
    roleDataMap[NameSlot] = QVariant(name);
    roleDataMap[MeshSlot] = QVariant(mesh);
    roleDataMap[GuidSlot] = QVariant(guid);
    stream << roleDataMap;

    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(format()), encoded);
    return mime;
}

bool isAssetDrag(const QMimeData *mime)
{
    return mime && mime->hasFormat(QString::fromLatin1(format()));
}

QMap<int, QVariant> roles(const QMimeData *mime)
{
    if (!isAssetDrag(mime)) return {};
    QByteArray encoded = mime->data(QString::fromLatin1(format()));
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QMap<int, QVariant> roleDataMap;
    // A LOOP, not one read: Qt's own item views write one map per selected
    // ROW, and the viewport's decoder has always taken the last one. Kept
    // exactly, so a multi-row drag out of a QListWidget behaves as it did.
    while (!stream.atEnd()) stream >> roleDataMap;
    return roleDataMap;
}

int typeOf(const QMimeData *mime)
{
    const QMap<int, QVariant> role = roles(mime);
    return role.contains(TypeSlot) ? role.value(TypeSlot).toInt() : -1;
}

QString guidOf(const QMimeData *mime)
{
    return roles(mime).value(GuidSlot).toString();
}

} // namespace AssetDrag
