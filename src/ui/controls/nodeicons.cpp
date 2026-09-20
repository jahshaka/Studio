/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/nodeicons.h"

#include <QHash>

#include "irisgl/core/irisutils.h"

namespace {

/// Both states of an icon under one picture: QIcon::Selected is set explicitly
/// so Qt does not repaint the pixmap with its own highlight tint on a selected
/// row (the reason the outliner did it by hand).
QIcon buildIcon(const QString &relativePath)
{
    QIcon icon;
    if (relativePath.isEmpty()) return icon;
    const QString path = IrisUtils::getAbsoluteAssetPath(relativePath);
    icon.addPixmap(path, QIcon::Normal);
    icon.addPixmap(path, QIcon::Selected);
    return icon;
}

const QIcon &cached(const QString &relativePath)
{
    // Leaked on purpose — see the header.
    static QHash<QString, QIcon> &icons = *new QHash<QString, QIcon>;
    auto it = icons.find(relativePath);
    if (it == icons.end()) it = icons.insert(relativePath, buildIcon(relativePath));
    return it.value();
}

}   // namespace

const QIcon &nodeicons::forType(iris::SceneNodeType type)
{
    static const QHash<iris::SceneNodeType, QString> kTypeIcon = {
        { iris::SceneNodeType::Mesh,           QStringLiteral("app/icons/icons8-mesh-32.png") },
        { iris::SceneNodeType::Light,          QStringLiteral("app/icons/icons8-sun-48.png") },
        { iris::SceneNodeType::ParticleSystem, QStringLiteral("app/icons/icons8-snow-storm-26.png") },
        { iris::SceneNodeType::Empty,          QStringLiteral("app/icons/icons8-average-math-filled-50.png") },
        { iris::SceneNodeType::Decal,          QStringLiteral("app/icons/icons8-picture-50.png") },
        { iris::SceneNodeType::Camera,         QStringLiteral("app/icons/icons8-camera-48.png") },
    };
    return cached(kTypeIcon.value(type));
}

const QIcon &nodeicons::visibility(bool visible)
{
    return cached(visible ? QStringLiteral("app/icons/icons8-eye-48.png")
                          : QStringLiteral("app/icons/icons8-eye-48-dim.png"));
}

const QIcon &nodeicons::lock(bool locked)
{
    return cached(locked ? QStringLiteral("app/icons/lock-filled.png")
                         : QStringLiteral("app/icons/lock-dim.png"));
}
