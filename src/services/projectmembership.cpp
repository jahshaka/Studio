/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectmembership.h"

ProjectMembership *ProjectMembership::instance()
{
    // Never destroyed: the process's last receivers die with the main window,
    // and a static QObject destroyed after QApplication is the classic
    // shutdown crash.
    static ProjectMembership *membership = new ProjectMembership();
    return membership;
}
