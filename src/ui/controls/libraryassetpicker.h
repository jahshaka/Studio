/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIBRARYASSETPICKER_H
#define LIBRARYASSETPICKER_H

// Pick one LIBRARY asset of a given type, by guid.
//
// Deliberately not AssetPickerWidget: that one lists the SESSION registrations
// (AssetManager), which only exist for types with a session shape — a light
// profile has none, and never will. This lists library rows straight out of the
// catalog and hands back a guid; whoever binds it resolves the bytes through
// the CAS. It is also deliberately not TexturePickerWidget, which still joins
// project->getProjectFolder() to build a path (the flat per-project layout the
// asset-pipeline program removed — LIGHTS_COMPLETION_SPEC §5.8, a pre-existing
// defect this lane must not inherit).

#include <QDialog>
#include <QString>

#include "data/project.h"

class Database;
class QListWidget;
class QLineEdit;

class LibraryAssetPicker : public QDialog
{
    Q_OBJECT

public:
    /// Modal. Returns the chosen asset's guid, or an empty string on cancel.
    static QString pick(ModelTypes type, Database *db, const QString &title,
                        QWidget *parent = nullptr);

private:
    LibraryAssetPicker(ModelTypes type, Database *db, const QString &title, QWidget *parent);
    void populate(const QString &filter);

    ModelTypes   mType;
    Database    *mDb = nullptr;
    QListWidget *mList = nullptr;
    QLineEdit   *mSearch = nullptr;
    QString      mChosen;
};

#endif // LIBRARYASSETPICKER_H
