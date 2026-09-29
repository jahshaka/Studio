/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/


#ifndef PROJECTMATERIALCOPYCOMMAND_H
#define PROJECTMATERIALCOPYCOMMAND_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QUndoCommand>

class Database;
class Project;

// A NEW PROJECT MATERIAL FROM AN EXISTING ONE, AS ONE UNDO STEP
// (MATERIAL-DROP-1 / TRAY-DUPLICATE-1, the owner 2026-09-28).
//
// THE RULE it carries: the LIBRARY is the source. A material dropped from the
// library onto an asset is a fresh, PRISTINE copy in the project — the library's
// current definition, whatever the project did to an earlier copy of the same
// entry — named by the project's own naming (`materialmembers::projectCopyName`:
// "Wood PBR", then "Wood PBR 2", "3", …). The tray's Duplicate is the same
// command made from one of the project's own materials (its version, edits and
// all). Either way the copy is the project's OWN row (ASSETS-SCOPE-1), pinned by
// it, sharing the source's member textures (the bundle model), without a bake
// (a baked map is born inside exactly one material), and — when the source is a
// shipped preset or a copy of one — stamped with that preset as its master.
//
// THE GUID SURVIVES AN UNDO (PresetCopyCommand's rule): minted at construction
// and kept, so a redo re-makes exactly the row the commands pushed after this one
// (the apply of a drop) name. Undo takes the pin and the row away.
class ProjectMaterialCopyCommand : public QUndoCommand
{
public:
    /// `pristine`: copy the LIBRARY's definition (a library drop) rather than
    /// the version `project` renders (a duplicate of the project's own).
    ProjectMaterialCopyCommand(Database *db, Project *project, const QString &source,
                               bool pristine);

    void undo() override;
    void redo() override;

    /// Can a redo succeed at all (a project, a definition to copy, a name)?
    /// Asked before the push, so a refusal never becomes an empty undo step.
    bool ready() const { return !mProjectGuid.isEmpty() && !mDefinition.isEmpty() && !mName.isEmpty(); }

    /// The copy's guid, minted at construction — read it BEFORE the push (a
    /// refused push deletes the command). Whether the row exists is the
    /// catalog's answer after the push, never this object's.
    QString copyGuid() const { return mCopyGuid; }

    /// TAKES THE COPY BACK FOR GOOD (MATERIAL-DROP-1's refusal path): a drop
    /// whose apply refused after the copy was pushed into its macro. Undoes the
    /// mint now and makes every later undo/redo of the macro a no-op, so the
    /// step leaves no orphan material. Only for a command the stack TOOK.
    void retract();
    QString name() const { return mName; }
    QString error() const { return mError; }

private:
    Database   *mDb = nullptr;
    Project    *mProject = nullptr;
    QString     mProjectGuid;     ///< captured at construction: the project may close
    QString     mSource;
    QString     mMaster;          ///< the shipped preset behind the source, or empty
    QString     mName;            ///< decided once, at construction
    QJsonObject mDefinition;
    QByteArray  mThumbnail;
    QString     mCopyGuid;        ///< minted at construction, kept for every redo
    QString     mError;
    bool        mRetracted = false;
};

#endif // PROJECTMATERIALCOPYCOMMAND_H
