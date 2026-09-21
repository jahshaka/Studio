/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/presetedit.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

#include "commands/presetcopycommand.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/editgate.h"
#include "services/materialbundle.h"
#include "services/sceneeditservice.h"
#include "services/materialpresetassets.h"
#include "services/materialpresetseeder.h"
#include "services/undoservice.h"

namespace {

const QLatin1String kMasterKey("presetMaster");

QJsonObject propertiesOf(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return QJsonObject();
    return QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
}

} // namespace

namespace presetedit
{

QString masterOf(Database *db, const QString &guid)
{
    if (guid.isEmpty()) return QString();
    // A PRESET IS ITS OWN MASTER. Asked of the thing on screen, the honest
    // answer to "which shipped material is this?" is the preset itself — that
    // is what the module's banner quotes and what `materials.open` reports
    // before any copy exists.
    if (!MaterialBundle::shippedPresetName(guid).isEmpty()) return guid;
    const QString recorded = propertiesOf(db, guid).value(kMasterKey).toString();
    // A STALE LINK IS NO LINK. The key is written by the copy and nothing ever
    // rewrites it, so a value that is not a reserved preset guid (a library
    // carried over from a build with a different table) answers nothing rather
    // than a guid no drawer can resolve.
    return MaterialBundle::shippedPresetName(recorded).isEmpty() ? QString() : recorded;
}

QStringList copiesOf(Database *db, const QString &masterGuid)
{
    QStringList out;
    if (!db || masterGuid.isEmpty()) return out;
    for (const auto &row : db->fetchAssetsForAssetView()) {
        if (row.type != static_cast<int>(ModelTypes::Material)) continue;
        if (QJsonDocument::fromJson(row.properties).object().value(kMasterKey).toString()
            == masterGuid)
            out << row.guid;
    }
    return out;
}

QString projectCopyOf(Database *db, Project *project, const QString &masterGuid)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return QString();
    if (MaterialBundle::shippedPresetName(masterGuid).isEmpty()) return QString();
    const QString projectGuid = project->getProjectGuid();
    for (const QString &candidate : copiesOf(db, masterGuid))
        if (db->isAssetPinnedBy(projectGuid, candidate)) return candidate;
    return QString();
}

QString refusal(Database *db, Project *project, const QString &guid)
{
    Q_UNUSED(db);
    const QString shipped = MaterialBundle::shippedPresetName(guid);
    if (shipped.isEmpty()) return QString();
    if (project && !project->getProjectGuid().isEmpty()) return QString();
    // THE ONE REFUSAL LEFT, and it names the way out. A master is editable in
    // a PROJECT — that is where the copy lives — so with no project open there
    // is nowhere for the edit to go.
    return QObject::tr("'%1' is a material the app ships, and the library's copy of it is "
                       "read-only. Open a project and it is yours to edit there: the first "
                       "edit makes that project its own copy.").arg(shipped);
}

Target forEdit(Database *db, Project *project, const QString &guid,
               UndoService *undo, SceneEditService *sceneEdit)
{
    Target target;
    if (guid.isEmpty()) {
        target.error = QObject::tr("no material");
        return target;
    }
    // NOT A PRESET = NOTHING TO DO, and no database write: every edit door
    // calls this on every edit, so the ordinary case must cost a table lookup.
    if (MaterialBundle::shippedPresetName(guid).isEmpty()) {
        target.guid = guid;
        target.master = masterOf(db, guid);
        return target;
    }
    if (!db) {
        target.error = QObject::tr("no library");
        return target;
    }
    const QString why = refusal(db, project, guid);
    if (!why.isEmpty()) {
        target.master = guid;
        target.error = why;
        return target;
    }

    // THIS PROJECT MAY ALREADY HAVE ITS COPY (the Fable read's item 1, and it
    // is the second edit of every preset the owner touches). A preset TILE
    // carries the MASTER's guid wherever it is shown — the Presets drawer, the
    // tray, a drag payload — so the second double-click, the second
    // `materials.edit(<master>)` and a save under a still-open master tab all
    // arrive here naming the master again. Without this the copy-on-write ran
    // a SECOND time: two "Wood PBR" rows pinned, the new command finding no
    // master pin to move and no node to re-point, and the answer adopting
    // whichever row the catalog listed first — so the edit could land on a copy
    // no mesh wears. ANSWERED, not copied: the project's material IS the copy.
    {
        const QString mine = projectCopyOf(db, project, guid);
        if (!mine.isEmpty()) {
            target.guid = mine;
            target.master = guid;
            return target;            // `copied` false: nothing was minted
        }
    }

    // THE MASTER MUST EXIST BEFORE IT CAN BE COPIED. Seeding is on first USE
    // (services/materialpresetassets.h) and this is a use: a preset nobody has
    // applied yet has no row, no definition and no members, so the copy would
    // have nothing to carry. One importer at a time, as every other first-use
    // door does.
    MaterialPresetSeeder::instance().finishNow();
    QString error;
    if (MaterialPresetAssets::ensureSeeded(guid, db, &error).isEmpty()) {
        target.master = guid;
        target.error = error.isEmpty() ? QObject::tr("the preset could not be seeded") : error;
        return target;
    }

    // THE EDIT GATE, ASKED HERE (editgate.h) rather than left to the push:
    // `UndoService::push` DELETES a command the gate refuses, and this
    // function reads the copy's guid back off it. Asked only on the undoable
    // route, because that is the hand-edit route — a run's own verbs are
    // inside `enterVerb` and are never refused.
    if (undo && editgate::refuse()) {
        target.master = guid;
        target.error = QObject::tr("a script run owns the document");
        return target;
    }

    // THE RE-DRESS, as a callback: the meshes wearing the material take the
    // copy's. `forgetMaterialDressing` first, because the memo that keeps an
    // autosave from re-attaching unchanged materials would answer "nothing
    // moved" here — the two definitions ARE identical at this instant; what
    // moved is WHICH material the node wears.
    PresetCopyCommand::Redress redress;
    if (sceneEdit) {
        redress = [sceneEdit](const QString &materialGuid) {
            sceneEdit->forgetMaterialDressing(materialGuid);
            sceneEdit->refreshMaterialUsers(materialGuid);
        };
    }
    auto *command = new PresetCopyCommand(db, project, redress, guid);
    target.master = guid;
    if (undo) {
        // ONE UNDO STEP for the copy, the pin move and the use edges — and,
        // inside a script run or a gesture macro, the same step as the edit
        // that asked for it (a run is one open macro).
        undo->push(command);
        // THE ANSWER COMES FROM THE CATALOG, NOT FROM THE COMMAND: a
        // QUndoStack OWNS what it is handed and may destroy it inside push
        // (a merge, an obsolete command, the stack's own limit), so
        // dereferencing it afterwards is a dangling read waiting to happen.
        // The copy is the material this project now pins in the master's
        // place, which is a fact this function can ask for.
        target.guid = projectCopyOf(db, project, guid);
        if (target.guid.isEmpty())
            target.error = QObject::tr("'%1' could not be copied into this project")
                               .arg(MaterialBundle::shippedPresetName(guid));
    } else {
        // NO UNDO SERVICE (a headless slice): the copy still happens, and it
        // is simply not undoable — the same rule every gesture in such a
        // session follows.
        command->redo();
        target.guid = command->copyGuid();
        target.error = command->error();
        delete command;
    }
    target.copied = !target.guid.isEmpty();
    return target;
}

} // namespace presetedit
