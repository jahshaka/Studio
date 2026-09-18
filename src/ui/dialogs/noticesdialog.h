/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NOTICESDIALOG_H
#define NOTICESDIALOG_H

// NoticesDialog — THE THIRD-PARTY NOTICES PAGE (NOTICES-1, 2026-09-18), opened
// from About. A list of every vendored component this binary ships and, beside
// it, that component's licence TEXT as read out of its own vendored tree at
// build time (app/notices.h).
//
// It renders `notices::entries()` and nothing of its own: the same data
// `app.notices()` serves, so the page and the verb cannot disagree (the API's
// own rule — a capability is a verb first and a surface second). No raw
// stylesheet anywhere in it: ThemeRoles and the shared helpers, like every
// other dialog since the theme law.

#include <QDialog>

class QListWidget;
class QTextBrowser;
class QLabel;

class NoticesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NoticesDialog(QWidget *parent = nullptr);

    /// The component the list is showing (its manifest id) — what a test reads
    /// to prove the selection drives the text.
    QString currentId() const;
    /// Selects one component by id; false for an unknown one.
    bool selectComponent(const QString &id);
    /// The text on screen right now.
    QString shownText() const;

private:
    void showRow(int row);

    QListWidget  *mList = nullptr;
    QTextBrowser *mText = nullptr;
    QLabel       *mHeading = nullptr;
};

#endif   // NOTICESDIALOG_H
