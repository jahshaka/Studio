/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PAGEHOST_H
#define PAGEHOST_H

// PageHost — the shell's pages, KEYED BY ID (D10-SHELL-MODULES; audit S11).
//
// The window's stacked widget used to be addressed by literal indices
// (`setCurrentIndex(4)` was the Player), and five of the seven space enum values
// did not equal their stack index — a page inserted anywhere but the end
// switched every space above it to the wrong widget (AVATAR_MODULE_SPEC R0.14
// said so in a comment, because nothing else could). Here a page is named by
// its space / module id ("desktop", "editor", "assets", "materials", "player",
// "publish", "avatar"), and the order pages were added in means nothing.

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QStackedWidget;
class QWidget;

class PageHost : public QObject
{
    Q_OBJECT
public:
    /// `stack` holds the pages.
    explicit PageHost(QStackedWidget *stack, QObject *parent = nullptr);

    /// Adds `page` under `id`. A second page under the same id is refused.
    bool addPage(const QString &id, QWidget *page);
    /// Swaps the page under `id` for `page` IN PLACE (the Assets page, built
    /// on its first showing, replaces its placeholder) and schedules the old
    /// widget for deletion. False for an unknown id.
    bool replacePage(const QString &id, QWidget *page);
    /// The page under `id`, or null.
    QWidget *page(const QString &id) const;
    bool hasPage(const QString &id) const { return mPages.contains(id); }

    /// Makes `id` the page on screen.
    /// `focus` gives the page the keyboard, as the Materials and Avatar spaces
    /// always did. False for an unknown id.
    bool show(const QString &id, bool focus = false);
    /// The id of the page on screen ("" before any page exists).
    QString currentId() const;
    bool isCurrent(const QString &id) const { return currentId() == id; }
    QWidget *currentPage() const;

    QStackedWidget *stack() const { return mStack; }

private:
    QStackedWidget *mStack = nullptr;
    QHash<QString, QPointer<QWidget>> mPages;
};

#endif // PAGEHOST_H
