/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/libraryassetpicker.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

#include "data/database/database.h"
#include "ui/controls/librarymodel.h"

QString LibraryAssetPicker::pick(ModelTypes type, Database *db, const QString &title,
                                 QWidget *parent)
{
    LibraryAssetPicker dialog(type, db, title, parent);
    if (dialog.exec() != QDialog::Accepted) return QString();
    return dialog.mChosen;
}

LibraryAssetPicker::LibraryAssetPicker(ModelTypes type, Database *db, const QString &title,
                                       QWidget *parent)
    : QDialog(parent), mType(type), mDb(db)
{
    setWindowTitle(title);
    resize(420, 480);

    auto *layout = new QVBoxLayout(this);
    mSearch = new QLineEdit(this);
    mSearch->setPlaceholderText(tr("Search\u2026"));
    layout->addWidget(mSearch);

    mModel = new LibraryModel(this);
    mModel->setTileSize(QSize(48, 48));
    mProxy = new LibraryFilterProxy(this);
    mProxy->setSourceModel(mModel);
    mProxy->setTypes({ static_cast<int>(type) });
    if (mDb) {
        QVector<LibraryRow> rows;
        for (const AssetRecord &record : mDb->fetchAssetsForAssetView()) {
            LibraryRow row;
            row.guid = record.guid;
            row.name = record.name;
            row.type = record.type;
            row.collection = record.collection;
            rows.append(row);
        }
        mModel->setRows(rows);
    }

    mList = new QListView(this);
    mList->setModel(mProxy);
    mList->setModelColumn(LibraryModel::NameColumn);
    mList->setIconSize(QSize(48, 48));
    mList->setSpacing(2);
    mList->setUniformItemSizes(true);
    mList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(mList, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    auto accept = [this, buttons](const QModelIndex &current) {
        if (!current.isValid()) return;
        // The FULL name is the row's: the list shows the base name.
        mChosen = current.data(LibraryModel::GuidRole).toString();
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!mChosen.isEmpty());
    };
    connect(mList->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [accept](const QModelIndex &current, const QModelIndex &) { accept(current); });
    connect(mList, &QListView::doubleClicked, this, [this, accept](const QModelIndex &index) {
        accept(index);
        if (!mChosen.isEmpty()) QDialog::accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(mSearch, &QLineEdit::textChanged, mProxy, &LibraryFilterProxy::setSearch);

    if (mProxy->rowCount() == 0) {
        auto *empty = new QLabel(tr("Nothing of this kind is in the library yet \u2014 import one "
                                    "from the Assets page first."), this);
        empty->setWordWrap(true);
        layout->insertWidget(1, empty);
    }
}
