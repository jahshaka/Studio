/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINWINDOWVIEWPORT_H
#define MAINWINDOWVIEWPORT_H

#include <QMainWindow>

// This is nothing more than a "proxy" or passthru widget so we can promote a regular QWidget
// TODO - remove and construct MainWindow interface programmatically (iKlsR)
class MainWindowViewPort : public QMainWindow
{
public:
    MainWindowViewPort();
};

#endif // MAINWINDOWVIEWPORT_H
