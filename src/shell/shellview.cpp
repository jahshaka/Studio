/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/shellview.h"

#include "shell/mainwindow.h"
#include "shell/spaces.h"

QWidget *ShellView::window() const
{
    return mWindow;
}

QString ShellView::space() const
{
    return spaces::id(mWindow->getWindowSpace());
}

bool ShellView::setSpace(const QString &space)
{
    WindowSpaces target;
    if (!spaces::fromId(space, &target)) return false;
    mWindow->switchSpace(target);
    return true;
}

void ShellView::showViewportToast(const QString &title, const QString &text)
{
    mWindow->showViewportToast(title, text);
}
