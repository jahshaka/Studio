/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLVIEW_H
#define SHELLVIEW_H

// ShellView — the shell's implementation of IShellView (ui/ishellview.h): a
// thin adapter that answers each call from the shell part that owns it, so
// the window class does not grow a forwarding method per verb.

#include "ui/ishellview.h"

class MainWindow;

class ShellView : public IShellView
{
public:
    explicit ShellView(MainWindow *window) : mWindow(window) {}

    QWidget *window() const override;
    QString space() const override;
    bool setSpace(const QString &space) override;
    void showViewportToast(const QString &title, const QString &text) override;

private:
    MainWindow *mWindow = nullptr;
};

#endif // SHELLVIEW_H
