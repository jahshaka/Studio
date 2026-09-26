/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/presetcopycommand.h"

#include <QJsonDocument>
#include <QSqlDatabase>
#include <QObject>

#include "data/guidmanager.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/assetdelete.h"
#include "services/materialbundle.h"
#include "services/projectassets.h"

namespace {

/// The row's `properties` JSON, as an object.
QJsonObject propertiesOf(Database *db, const QString &guid)
{
    if (!db) return QJsonObject();
    return QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
}

} // namespace

PresetCopyCommand::PresetCopyCommand(Database *db, Project *project, Redress redress,
                                     const QString &master)
    : QUndoCommand(QObject::tr("Edit material")),
      mDb(db), mProject(project), mRedress(std::move(redress)), mMaster(master),
      mProjectGuid(project ? project->getProjectGuid() : QString())
{
    if (!mDb || mMaster.isEmpty()) return;
    // THE MASTER IS READ ONCE, HERE, and the copy is made from THAT — not from
    // whatever the row holds at the next redo. The definition is what the
    // project is rendering right now: pin-first, like every other read of a
    // bundle (`MaterialBundle::read`), so a project holding an older pin
    // copies the version it can see rather than a newer library one.
    mDefinition = MaterialBundle::read(mDb, mMaster, mProject);
    const AssetRecord row = mDb->fetchAsset(mMaster);
    mName = row.name.isEmpty() ? MaterialBundle::shippedPresetName(mMaster) : row.name;
    // The preset's tile stands in until something draws the copy (THUMBS-1:
    // a render is asked for by whoever made the gesture).
    mThumbnail = row.thumbnail;
    // THE NAME IS THE PRESET'S, and it is the definition's too: the graph's
    // settings carry a name that `buildDefinition` writes back on every save,
    // so a definition left naming the master would rename the copy at its
    // first save — the defect Customise had (PRESET-UNIFY-1).
    if (!mName.isEmpty()) {
        mDefinition[QStringLiteral("name")] = mName;
        if (mDefinition.contains(QStringLiteral("shadergraph"))) {
            QJsonObject graph = mDefinition.value(QStringLiteral("shadergraph")).toObject();
            QJsonObject settings = graph.value(QStringLiteral("settings")).toObject();
            settings[QStringLiteral("name")] = mName;
            graph[QStringLiteral("settings")] = settings;
            mDefinition[QStringLiteral("shadergraph")] = graph;
        }
    }
}

void PresetCopyCommand::redo()
{
    mError.clear();
    if (!mDb || mMaster.isEmpty() || mProjectGuid.isEmpty()) {
        mError = QObject::tr("no project");
        return;
    }
    if (mDefinition.isEmpty()) {
        mError = QObject::tr("'%1' has no definition to copy").arg(mName);
        return;
    }

    // ONE TRANSACTION FOR THE CATALOG HALF (the Fable read's item 4, and the
    // same rule `MaterialBundle::write` follows): the mint, the master link,
    // the pin, the unpin and the use edges are five catalog writes that mean
    // nothing apart — a failure between any two of them would leave a project
    // pinning two materials, or none, or a mesh wearing a row that is not
    // there. The guard rolls back at destruction unless `commit()` is reached,
    // and it degrades to a no-op inside somebody else's transaction (a gesture
    // batch, an import slice), which is the correct nesting behaviour. The
    // BYTES the definition write publishes are content-addressed and are
    // collected by `assets.gc` if the rows never land — the harmless direction.
    QSqlDatabase conn = QSqlDatabase::database();
    DbTransaction tx(conn);
    const QString mintedBefore = mCopyGuid;

    // 1. THE COPY. The guid is minted once and kept, so a redo after an undo
    // re-makes the SAME material (see the header).
    const QString guid = mCopyGuid.isEmpty() ? GUIDManager::generateGUID() : mCopyGuid;
    QString error;
    const QString made = MaterialBundle::createPresetCopy(mDb, guid, mProjectGuid, mName,
                                                          mDefinition, mThumbnail, &error);
    if (made.isEmpty()) {
        mError = error.isEmpty() ? QObject::tr("the library refused the copy") : error;
        return;
    }
    mCopyGuid = made;

    // THE LINK BACK TO THE MASTER, on the row rather than in the definition:
    // it is a fact about this row in this library, it must survive every
    // later definition write, and it is what `presetedit::masterOf` reads.
    QJsonObject props = propertiesOf(mDb, mCopyGuid);
    props.insert(MaterialBundle::kPresetMasterKey, mMaster);
    mDb->updateAssetProperties(mCopyGuid, QJsonDocument(props).toJson());

    // 2. THE PIN MOVES. Direct, because the user asked for this material to be
    // in their project — it is the one they see in the drawer and the tray.
    ProjectAssets::addToProject(mCopyGuid, mDb, mProject, ProjectAssets::AddKind::Direct);
    mMasterWasPinned = mDb->isAssetPinnedBy(mProjectGuid, mMaster);
    if (mMasterWasPinned) assetdelete::removeFromProject(mDb, mMaster, mProjectGuid);

    // 3. THE USE EDGES…
    moveUseEdges(mMaster, mCopyGuid);
    if (!tx.commit()) {
        mError = QObject::tr("the catalog refused the copy");
        // A FIRST redo that failed made nothing, and `copyGuid()` must say so
        // (it is what `presetedit::forEdit` answers with on the route that has
        // no undo stack). A LATER one keeps the guid it established, because a
        // rolled-back redo has not changed which material this command IS.
        mCopyGuid = mintedBefore;
        return;
    }
    // …and 4. the re-dress, AFTER the commit: it reads the definition back
    // through the store and hands every mesh a new material, which is not work
    // to do inside a transaction that may still roll back.
    if (mRedress) mRedress(mCopyGuid);
}

void PresetCopyCommand::undo()
{
    if (!mDb || mCopyGuid.isEmpty() || mProjectGuid.isEmpty()) return;

    {
        // One transaction here too, for the reason it is one in `redo`: half an
        // undo is a project pinning a material whose row is gone.
        QSqlDatabase conn = QSqlDatabase::database();
        DbTransaction tx(conn);

        // Exactly the four steps, backwards. The edges first: nothing may point
        // at the copy by the time its row goes.
        moveUseEdges(mCopyGuid, mMaster);
        if (mMasterWasPinned)
            ProjectAssets::addToProject(mMaster, mDb, mProject, ProjectAssets::AddKind::Direct);

        // The project side, then the library row. `keepShared` true is the
        // point: the copy names the PRESET'S member textures, and they belong
        // to the preset — a closure delete here would take the master's
        // pictures with it.
        assetdelete::removeFromProject(mDb, mCopyGuid, mProjectGuid);
        assetdelete::remove(mDb, mCopyGuid, /*keepShared*/ true, /*force*/ true);
        tx.commit();
    }

    if (mRedress) mRedress(mMaster);
}

void PresetCopyCommand::moveUseEdges(const QString &from, const QString &to)
{
    if (!mDb || from.isEmpty() || to.isEmpty()) return;
    // "This node uses that material" — one edge per (node, material) pair, as
    // MaterialUseEdgeCommand writes them. The nodes are read from the CATALOG
    // rather than walked in the scene: the edge is what the tray, the closure
    // walkers and "used by" read, and a node the scene has since dropped must
    // not keep an edge to the copy.
    const QStringList nodes = mDb->fetchDependers(from, mProjectGuid);
    for (const QString &nodeGuid : nodes) {
        mDb->deleteDependency(nodeGuid, from);
        mDb->createDependency(static_cast<int>(ModelTypes::Object),
                              static_cast<int>(ModelTypes::Material),
                              nodeGuid, to, mProjectGuid);
    }
}
