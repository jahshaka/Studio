/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/pagehost.h"

#include <QStackedWidget>

PageHost::PageHost(QStackedWidget *stack, QObject *parent)
    : QObject(parent), mStack(stack)
{
}

bool PageHost::addPage(const QString &id, QWidget *page)
{
    if (!mStack || !page || id.isEmpty() || mPages.contains(id)) return false;
    mStack->addWidget(page);
    mPages.insert(id, page);
    return true;
}

bool PageHost::replacePage(const QString &id, QWidget *page)
{
    QWidget *old = this->page(id);
    if (!mStack || !old || !page) return false;
    const int at = mStack->indexOf(old);
    const bool wasCurrent = mStack->currentWidget() == old;
    mStack->removeWidget(old);
    mStack->insertWidget(at, page);
    if (wasCurrent) mStack->setCurrentWidget(page);
    mPages.insert(id, page);
    old->deleteLater();
    return true;
}

QWidget *PageHost::page(const QString &id) const
{
    return mPages.value(id).data();
}

bool PageHost::show(const QString &id, bool focus)
{
    QWidget *target = page(id);
    if (!mStack || !target) return false;
    mStack->setCurrentWidget(target);
    if (focus) target->setFocus();
    return true;
}

QString PageHost::currentId() const
{
    const QWidget *current = mStack ? mStack->currentWidget() : nullptr;
    if (!current) return QString();
    for (auto it = mPages.cbegin(); it != mPages.cend(); ++it)
        if (it.value().data() == current) return it.key();
    return QString();
}

QWidget *PageHost::currentPage() const
{
    return mStack ? mStack->currentWidget() : nullptr;
}


