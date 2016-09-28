/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "project.h"
#include <QFileInfo>
#include <QDir>

Project::Project()
{
    _saved = false;
}

void Project::setFilePath(QString filePath)
{
    QFileInfo info(filePath);

    this->filePath = filePath;
    folderPath = info.absolutePath();
    projectName = info.baseName();
    fileName = info.completeBaseName();
    _saved = true;
}

bool Project::isSaved()
{
    return _saved;
}

QString Project::getFilePath()
{
    return filePath;
}

QString Project::getFileName()
{
    return fileName;
}

QString Project::getProjectName()
{
    return projectName;
}

QString Project::getProjectFolder()
{
    return folderPath;
}


Project* Project::createNew()
{
    auto project = new Project;
    project->folderPath = QDir::currentPath();
    project->fileName = "";
    project->projectName = "Untitled";

    return project;
}
