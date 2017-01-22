/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECT_H
#define PROJECT_H
#include <QString>

class Project
{
    QString folderPath;
    QString fileName;
    QString projectName;
    QString filePath;

    bool _saved;
public:
    Project();
    void setFilePath(QString filePath);
    bool isSaved();

    QString getFilePath();
    QString getFileName();
    QString getProjectName();
    QString getProjectFolder();

    static Project* createNew();
};

#endif // PROJECT_H
