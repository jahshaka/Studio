/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LOADMESHDIALOG_H
#define LOADMESHDIALOG_H

#include <QDialog>

namespace Ui {
class LoadMeshDialog;
}

class LoadMeshDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoadMeshDialog(QWidget *parent = 0);
    ~LoadMeshDialog();

    QString getTextureFile();
    QString getMeshFile();
private slots:
    void loadMesh();
    void loadTexture();

private:
    Ui::LoadMeshDialog *ui;

    QString texFile;
    QString meshFile;
};

#endif // LOADMESHDIALOG_H
