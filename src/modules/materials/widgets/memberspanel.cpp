/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "memberspanel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "data/database/database.h"
#include "data/project.h"
#include "services/materialmembers.h"

namespace {

constexpr int kGuidRole = Qt::UserRole + 1;
constexpr int kUsedByRole = Qt::UserRole + 2;

QString humanSize(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("—");
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

}   // namespace

namespace materials {

MembersPanel::MembersPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    mSummary = new QLabel(tr("No material open"));
    mSummary->setWordWrap(true);
    layout->addWidget(mSummary);

    mList = new QTreeWidget;
    mList->setRootIsDecorated(false);
    mList->setUniformRowHeights(true);
    mList->setSelectionMode(QAbstractItemView::SingleSelection);
    mList->setColumnCount(5);
    mList->setHeaderLabels({ tr("Name"), tr("Slot"), tr("Size"), tr("Used by"), tr("Kind") });
    mList->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(mList, 1);

    auto *buttons = new QHBoxLayout;
    mCleanButton = new QPushButton(tr("Clean unused…"));
    mUniqueButton = new QPushButton(tr("Make unique"));
    mUniqueButton->setToolTip(tr("Give this material its own copy of the picture, so editing it "
                                 "here changes nothing else."));
    mCleanButton->setEnabled(false);
    mUniqueButton->setEnabled(false);
    buttons->addWidget(mCleanButton);
    buttons->addWidget(mUniqueButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(mCleanButton, &QPushButton::clicked, this, &MembersPanel::cleanUnused);
    connect(mUniqueButton, &QPushButton::clicked, this, &MembersPanel::makeUnique);
    connect(mList, &QTreeWidget::itemSelectionChanged, this, &MembersPanel::selectionChanged);
}

void MembersPanel::setMaterial(const QString &guid)
{
    mMaterial = guid;
    refresh();
}

void MembersPanel::refresh()
{
    mList->clear();
    mUniqueButton->setEnabled(false);
    if (!mDb || mMaterial.isEmpty()) {
        mSummary->setText(tr("No material open"));
        mCleanButton->setEnabled(false);
        return;
    }

    const auto members = materialmembers::describe(mDb, mProject, mMaterial);
    for (const auto &member : members) {
        auto *item = new QTreeWidgetItem(mList);
        item->setText(0, member.name);
        // SLOT OR NODE — the one column, because a member is in one or the
        // other: a master slot, or a texture node of the graph that has not
        // reached a slot yet.
        item->setText(1, !member.slot.isEmpty() ? member.slot
                                                : (member.node.isEmpty()
                                                       ? QString()
                                                       : tr("node %1").arg(member.node)));
        item->setText(2, humanSize(member.bytes));
        item->setText(3, QString::number(member.usedBy));
        item->setText(4, member.role == QLatin1String("baked") ? tr("baked") : tr("picture"));
        item->setData(0, kGuidRole, member.guid);
        item->setData(0, kUsedByRole, member.usedBy);
        if (member.usedBy > 1)
            item->setToolTip(3, tr("Shared with %1 other material(s). Make unique gives this "
                                   "material its own copy.").arg(member.usedBy - 1));
    }

    // THE QUIET BADGE (owner Q6): a count, never an action. The button is how
    // anything goes, and it lists first.
    const int unusedCount = materialmembers::unused(mDb, mProject, mMaterial).size();
    mCleanButton->setEnabled(unusedCount > 0);
    mSummary->setText(unusedCount > 0
                          ? tr("%1 member(s) · %2 unused").arg(members.size()).arg(unusedCount)
                          : tr("%1 member(s)").arg(members.size()));
}

void MembersPanel::selectionChanged()
{
    const auto *item = mList->currentItem();
    // MAKE UNIQUE MEANS SOMETHING ONLY FOR A SHARED PICTURE. On a member this
    // material alone uses, a second row over the same bytes would be a row
    // nobody asked for.
    mUniqueButton->setEnabled(item && item->data(0, kUsedByRole).toInt() > 1);
}

void MembersPanel::cleanUnused()
{
    if (!mDb || mMaterial.isEmpty()) return;
    const auto candidates = materialmembers::unused(mDb, mProject, mMaterial);
    if (candidates.isEmpty()) { refresh(); return; }

    // LIST FIRST — the owner's Q6, and the store's own law. The user sees the
    // names before anything happens.
    QStringList names;
    for (const auto &entry : candidates) names << entry.name;
    QMessageBox box(this);
    box.setWindowTitle(tr("Clean unused"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("Remove %1 texture(s) nothing uses from this material?").arg(candidates.size()));
    box.setInformativeText(names.join(QStringLiteral("\n")));
    box.setDetailedText(tr("The library rows go. The image FILES stay in the store until you run "
                           "a store clean-up (assets.gc), which lists before it removes too."));
    box.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Yes) return;

    QString error;
    materialmembers::cleanUnused(mDb, mProject, mMaterial, &error);
    refresh();
    emit membersChanged(mMaterial);
}

void MembersPanel::makeUnique()
{
    const auto *item = mList->currentItem();
    if (!mDb || mMaterial.isEmpty() || !item) return;
    QString error;
    const QString made = materialmembers::makeUnique(mDb, mProject, mMaterial,
                                                     item->data(0, kGuidRole).toString(), &error);
    if (made.isEmpty()) {
        QMessageBox::warning(this, tr("Make unique"),
                             tr("That picture could not be copied: %1").arg(error));
        return;
    }
    refresh();
    emit membersChanged(mMaterial);
}

}   // namespace materials
