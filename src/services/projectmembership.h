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

/// A PROJECT'S TRAY MAY HAVE CHANGED — a pin was added or taken away, or a
/// dependency edge (a USE: a node's material slot, an image plane, a decal,
/// an applied material) was written or removed. The editor's asset tray
/// repopulates from it, so an asset added through a verb, a binding or a
/// paste shows up without a click (lane L13: `assets.addToProject` left the
/// tray stale until the user clicked a folder). Announced by the functions
/// that make the change — ProjectAssets::addToProject,
/// assetdelete::removeFromProject and, for edges, Database's dependency
/// mutators (through its listener, installed with the first instance()) —
/// never by their callers, so no path can forget it. An EMPTY project guid
/// means "some project" (an edge delete that cannot name one). Process-wide
/// (one editor per process); receivers connect with a context object and are
/// dropped with it.
class ProjectMembership : public QObject
{
    Q_OBJECT
public:
    static ProjectMembership *instance();
    /// A pin change for `projectGuid` (ignored when empty).
    void announce(const QString &projectGuid)
    {
        if (!projectGuid.isEmpty()) emit changed(projectGuid);
    }
    /// A change for `projectGuid`, or for an unnamed project when empty.
    void announceAny(const QString &projectGuid) { emit changed(projectGuid); }

signals:
    void changed(const QString &projectGuid);

private:
    ProjectMembership() = default;
};

#endif // PROJECTMEMBERSHIP_H
