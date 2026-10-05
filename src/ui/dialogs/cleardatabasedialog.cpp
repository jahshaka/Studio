/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/cleardatabasedialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

ClearDatabaseDialog::ClearDatabaseDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("clearDatabaseDialog"));
    setWindowTitle(tr("Reset your library"));

    auto *layout = new QVBoxLayout(this);

    auto *text = new QLabel(
        tr("This clears your library and starts Jahshaka again as a first launch:\n\n"
           "  • every project, and its folder on disk\n"
           "  • the app's own content (it is set up again) and every cached thumbnail\n\n"
           "Your Assets, your avatars and the materials you made in the Materials module are "
           "KEPT unless you tick a box below. Your preferences are kept. Nothing here can be "
           "undone."),
        this);
    text->setWordWrap(true);
    layout->addWidget(text);

    mClearAssets = new QCheckBox(tr("Also clear Assets (everything you imported or saved there, "
                                    "and your avatars)"),
                                 this);
    mClearAssets->setObjectName(QStringLiteral("clearAssets"));
    mClearAssets->setChecked(false);
    layout->addWidget(mClearAssets);

    mClearMaterials = new QCheckBox(tr("Also clear Materials (every material you made in the "
                                       "Materials module)"),
                                    this);
    mClearMaterials->setObjectName(QStringLiteral("clearMaterials"));
    mClearMaterials->setChecked(false);
    layout->addWidget(mClearMaterials);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *reset = buttons->addButton(tr("Reset and restart"),
                                            QDialogButtonBox::DestructiveRole);
    reset->setObjectName(QStringLiteral("resetLibrary"));
    QPushButton *cancel = buttons->addButton(QDialogButtonBox::Cancel);
    cancel->setDefault(true);   // the safe answer is the one Enter gives
    connect(reset, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QVariantMap ClearDatabaseDialog::options() const
{
    return QVariantMap{ { QStringLiteral("clearAssets"), mClearAssets->isChecked() },
                        { QStringLiteral("clearMaterials"), mClearMaterials->isChecked() } };
}
