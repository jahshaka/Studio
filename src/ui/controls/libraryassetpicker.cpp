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
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "data/database/database.h"

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
    mSearch->setPlaceholderText(tr("Search…"));
    layout->addWidget(mSearch);

    mList = new QListWidget(this);
    mList->setIconSize(QSize(48, 48));
    mList->setSpacing(2);
    layout->addWidget(mList, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    auto accept = [this, buttons]() {
        if (auto *item = mList->currentItem()) {
            mChosen = item->data(Qt::UserRole).toString();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!mChosen.isEmpty());
        }
    };
    connect(mList, &QListWidget::currentItemChanged, this, [accept](QListWidgetItem *, QListWidgetItem *) { accept(); });
    connect(mList, &QListWidget::itemDoubleClicked, this, [this, accept](QListWidgetItem *) {
        accept();
        if (!mChosen.isEmpty()) QDialog::accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(mSearch, &QLineEdit::textChanged, this, [this](const QString &text) { populate(text); });

    populate(QString());
    if (mList->count() == 0) {
        auto *empty = new QLabel(tr("Nothing of this kind is in the library yet — import one "
                                    "from the Assets page first."), this);
        empty->setWordWrap(true);
        layout->insertWidget(1, empty);
    }
}

void LibraryAssetPicker::populate(const QString &filter)
{
    mList->clear();
    if (!mDb) return;
    for (const auto &record : mDb->fetchAssetsForAssetView()) {
        if (static_cast<ModelTypes>(record.type) != mType) continue;
        if (!filter.isEmpty() && !record.name.contains(filter, Qt::CaseInsensitive)) continue;
        auto *item = new QListWidgetItem(record.name);
        if (!record.thumbnail.isEmpty()) {
            QPixmap pixmap;
            if (pixmap.loadFromData(record.thumbnail)) item->setIcon(QIcon(pixmap));
        }
        item->setData(Qt::UserRole, record.guid);
        mList->addItem(item);
    }
}
