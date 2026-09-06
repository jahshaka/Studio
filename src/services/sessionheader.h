/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SESSIONHEADER_H
#define SESSIONHEADER_H

// The startup header block (SESSION_LOG_SPEC §4) — UE's
// FApp::PrintStartupLogMessages equivalent, and the reason F1-A (one file per
// RUN) is the right fork: the build id, the GPU and the settings that produced
// a failure have to be in the same artifact as the failure.
//
// SPLIT DELIBERATELY. The rows this TU produces itself need nothing but Qt and
// the version constants, so a suite can link it alone. Everything that needs
// the engine, the asset store or the shader cache arrives through
// addProvider() — the app registers those in main(), the log.core suite does
// not, and neither one has to fake the other's half.
//
// Node/material/light counts are NOT here: no scene is open yet. They belong to
// the scene-open block (§5).

#include <QList>
#include <QPair>
#include <QString>

#include <functional>

namespace SessionHeader {

using Row = QPair<QString, QString>;
using Rows = QList<Row>;
using Provider = std::function<Rows()>;

/// Adds a group of rows to the header. Groups are emitted in registration
/// order, after the base rows. Providers run once, when emit() is called.
void addProvider(const QString &group, Provider provider);

/// The rows this TU can produce with no app dependencies: version, build id,
/// build type, Qt (compiled and runtime), platform/QPA, command line, data
/// root, settings file.
Rows baseRows();

/// Every row, base first then each provider's, each prefixed with its group.
Rows allRows();

/// Writes the whole header into JahLog under `app`/`Display`.
void emitBlock();

}   // namespace SessionHeader

#endif   // SESSIONHEADER_H
