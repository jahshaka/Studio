/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTMEMBERSHIP_H
#define PROJECTMEMBERSHIP_H

#include <QObject>
#include <QString>

/// A PROJECT'S MEMBERSHIP CHANGED — a pin was added or taken away. The
/// editor's asset tray repopulates from it, so an asset added through a verb,
/// a binding (a decal's image, a particle image, a light profile) or a paste
/// shows up without a click (lane L13: `assets.addToProject` left the tray
/// stale until the user clicked a folder). Announced by the two functions
/// that change an open project's pins — ProjectAssets::addToProject and
/// assetdelete::removeFromProject — never by their callers, so no path can
/// forget it. Process-wide (one editor per process); receivers connect with a
/// context object and are dropped with it.
class ProjectMembership : public QObject
{
    Q_OBJECT
public:
    static ProjectMembership *instance();
    void announce(const QString &projectGuid)
    {
        if (!projectGuid.isEmpty()) emit changed(projectGuid);
    }

signals:
    void changed(const QString &projectGuid);

private:
    ProjectMembership() = default;
};

#endif // PROJECTMEMBERSHIP_H
