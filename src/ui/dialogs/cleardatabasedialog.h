/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLEARDATABASEDIALOG_H
#define CLEARDATABASEDIALOG_H

// CLEAR DATABASE (ASSETS-HOME-1; the owner, 2026-10-04): a confirmation with
// two boxes, "Also clear Assets" and "Also clear Materials", BOTH OFF by
// default. Everything else is always cleared; a ticked box also clears that
// storage. The dialog only asks — `options()` is what the caller hands the one
// verb, `app.resetLibrary` (API-first).

#include <QDialog>
#include <QVariantMap>

class QCheckBox;

class ClearDatabaseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ClearDatabaseDialog(QWidget *parent = nullptr);

    /// `{clearAssets, clearMaterials}` as the boxes stand.
    QVariantMap options() const;

    QCheckBox *assetsBox() const { return mClearAssets; }
    QCheckBox *materialsBox() const { return mClearMaterials; }

private:
    QCheckBox *mClearAssets = nullptr;
    QCheckBox *mClearMaterials = nullptr;
};

#endif // CLEARDATABASEDIALOG_H
