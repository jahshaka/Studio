/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MODULEREGISTRY_H
#define MODULEREGISTRY_H

// THE COMPILED-IN MODULE LIST (studiomodule.h: registration is static by
// design). The shell asks for the list and never names a module's class —
// `mainwindow.cpp includes no module header` is a hygiene row.

#include <QVector>

class StudioModule;

namespace moduleregistry {

/// A fresh instance of every module, in the order the shell initializes them
/// (and the order their verbs land in the registry). The caller owns them.
QVector<StudioModule *> createAll();

}   // namespace moduleregistry

#endif // MODULEREGISTRY_H
