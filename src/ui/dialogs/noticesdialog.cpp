/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/dialogs/noticesdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "app/notices.h"
#include "ui/style/themeroles.h"

NoticesDialog::NoticesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Third-party notices"));
    resize(880, 560);

    mHeading = new QLabel(this);
    mHeading->setWordWrap(true);
    ThemeRoles::setTone(mHeading, ThemeRoles::Tone::Muted);

    mList = new QListWidget(this);
    mList->setMinimumWidth(240);
    ThemeRoles::setFrame(mList, QFrame::NoFrame);

    mText = new QTextBrowser(this);
    mText->setOpenExternalLinks(true);
    // A LICENCE IS PREFORMATTED TEXT: its line breaks and its indentation are
    // part of it, and a proportional font re-wrapping an MIT paragraph is a
    // licence displayed differently from the one in the tree.
    ThemeRoles::setMonospace(mText, 12);
    ThemeRoles::setFrame(mText, QFrame::NoFrame);

    auto *split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(mList);
    split->addWidget(mText);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(mHeading);
    layout->addWidget(split, 1);
    layout->addWidget(buttons);

    const QVector<notices::Entry> &all = notices::entries();
    mHeading->setText(tr("Jahshaka ships the work of others. Each component below is vendored in "
                         "this application's own source tree, and the licence text beside it is "
                         "read from that tree when the application is built — never copied by "
                         "hand. %n component(s).", nullptr, all.size()));
    for (const notices::Entry &e : all) {
        // The LIST says what the thing is for; the licence's own name goes
        // beside it because that is the one fact a person scanning for a
        // particular licence is looking for.
        QString label = e.name;
        if (!e.licence.isEmpty()) label += QStringLiteral("  —  ") + e.licence;
        if (!e.present) label += tr("  (not in this build)");
        auto *item = new QListWidgetItem(label, mList);
        item->setData(Qt::UserRole, e.id);
    }
    connect(mList, &QListWidget::currentRowChanged, this, &NoticesDialog::showRow);
    if (!all.isEmpty()) mList->setCurrentRow(0);
}

void NoticesDialog::showRow(int row)
{
    if (row < 0 || row >= notices::entries().size()) { mText->clear(); return; }
    const notices::Entry &e = notices::entries().at(row);
    // WHAT IT IS, WHERE IT CAME FROM, THEN THE TEXT — in that order, because a
    // licence on its own does not tell a user what the component does for them
    // or which directory it was read from.
    QString body;
    if (!e.role.isEmpty())     body += e.role + QStringLiteral("\n\n");
    if (!e.homepage.isEmpty()) body += e.homepage + QStringLiteral("\n");
    if (!e.path.isEmpty())     body += e.path + QStringLiteral("/") + e.file + QStringLiteral("\n");
    body += QStringLiteral("\n") + notices::text(e.id);
    mText->setPlainText(body);
    mText->moveCursor(QTextCursor::Start);
}

QString NoticesDialog::currentId() const
{
    auto *item = mList ? mList->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool NoticesDialog::selectComponent(const QString &id)
{
    if (!mList) return false;
    for (int i = 0; i < mList->count(); ++i) {
        if (mList->item(i)->data(Qt::UserRole).toString() != id) continue;
        mList->setCurrentRow(i);
        return true;
    }
    return false;
}

QString NoticesDialog::shownText() const
{
    return mText ? mText->toPlainText() : QString();
}
