/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SPACES_H
#define SPACES_H

// THE WINDOW'S SPACES, and the one place their names are spelled. A space's
// name is its page's id in the PageHost and, for a module space, the module's
// id — so there is no index anywhere to keep in step with an enum (audit S11).

#include <QString>

enum WindowSpaces : int {
    DESKTOP,
    PLAYER,
    EDITOR,
    EFFECT,
    ASSETS,
    PUBLISH,
    AVATAR
};

namespace spaces {

/// The space's name: the same words app.space() accepts.
inline QString id(WindowSpaces s)
{
    switch (s) {
    case WindowSpaces::DESKTOP: return QStringLiteral("desktop");
    case WindowSpaces::PLAYER:  return QStringLiteral("player");
    case WindowSpaces::EDITOR:  return QStringLiteral("editor");
    case WindowSpaces::EFFECT:  return QStringLiteral("materials");
    case WindowSpaces::ASSETS:  return QStringLiteral("assets");
    case WindowSpaces::PUBLISH: return QStringLiteral("publish");
    case WindowSpaces::AVATAR:  return QStringLiteral("avatar");
    }
    return QStringLiteral("?");
}

/// The space named `name` ("effects" is the Materials space's old name, still
/// accepted). False for a name that is not a space.
inline bool fromId(const QString &name, WindowSpaces *out)
{
    const QString s = name.trimmed().toLower();
    WindowSpaces space;
    if (s == QLatin1String("desktop"))                                       space = WindowSpaces::DESKTOP;
    else if (s == QLatin1String("player"))                                   space = WindowSpaces::PLAYER;
    else if (s == QLatin1String("editor"))                                   space = WindowSpaces::EDITOR;
    else if (s == QLatin1String("materials") || s == QLatin1String("effects")) space = WindowSpaces::EFFECT;
    else if (s == QLatin1String("assets"))                                   space = WindowSpaces::ASSETS;
    else if (s == QLatin1String("publish"))                                  space = WindowSpaces::PUBLISH;
    else if (s == QLatin1String("avatar"))                                   space = WindowSpaces::AVATAR;
    else return false;
    if (out) *out = space;
    return true;
}

}   // namespace spaces

#endif // SPACES_H
