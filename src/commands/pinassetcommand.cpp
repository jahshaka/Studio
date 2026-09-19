/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/pinassetcommand.h"

#include <QObject>

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetdelete.h"
#include "services/projectassets.h"

PinAssetCommand::PinAssetCommand(Database *db, Project *project, const QString &assetGuid)
    : QUndoCommand(QObject::tr("Add to project")),
      mDb(db), mProject(project), mAssetGuid(assetGuid),
      mProjectGuid(project ? project->getProjectGuid() : QString())
{
}

void PinAssetCommand::redo()
{
    mPinnedByUs = false;
    if (!mDb || mAssetGuid.isEmpty() || mProjectGuid.isEmpty()) return;
    // ALREADY A MEMBER = NOTHING TO DO, AND NOTHING TO UNDO. `addToProject`
    // is idempotent, but it re-pins to the library's CURRENT version — which
    // on a project holding an older pin (or its own copy-on-written one)
    // would silently upgrade it. The project's membership is not this
    // command's to change; only its ABSENCE is.
    if (mDb->isAssetPinnedBy(mProjectGuid, mAssetGuid)) return;

    // A BINDING, not a Direct add: something in the scene refers to this
    // asset, which is exactly what AddKind::Binding means (a Direct add of a
    // texture would mint a companion material nobody asked for).
    const ProjectAssets::Result result =
        ProjectAssets::addToProject(mAssetGuid, mDb, mProject, ProjectAssets::AddKind::Binding);
    mPinnedByUs = result.ok();
}

void PinAssetCommand::undo()
{
    if (!mPinnedByUs || !mDb || mAssetGuid.isEmpty() || mProjectGuid.isEmpty()) return;
    // The project side only: the pin on the asset and on the members only it
    // uses. A shared texture keeps its pin, the library row is untouched.
    assetdelete::removeFromProject(mDb, mAssetGuid, mProjectGuid);
    mPinnedByUs = false;
}
