/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectmembership.h"

#include "data/database/database.h"

ProjectMembership *ProjectMembership::instance()
{
    // Never destroyed: the process's last receivers die with the main window,
    // and a static QObject destroyed after QApplication is the classic
    // shutdown crash.
    static ProjectMembership *membership = [] {
        auto *created = new ProjectMembership();
        // A dependency edge is a USE, and the tray reads use from the edges:
        // an edge written for a project is announced like a pin change (empty
        // = "some project", when a delete cannot name one).
        Database::setDependencyListener([created](const QString &projectGuid) {
            created->announceAny(projectGuid);
        });
        return created;
    }();
    return membership;
}
