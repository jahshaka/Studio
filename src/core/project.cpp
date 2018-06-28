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

const QStringList Project::ModelTypesAsString = {
    "undefined",
    "material",
    "texture",
    "video",
    "cubemap",
    "object",
    "mesh",
    "sound_effect",
    "music",
    "shader",
    "variant",
    "file",
    "particle_system"
};

Project::Project()
{
    _saved = false;
}

void Project::setProjectPath(const QString &path, const QString &name)
{
    this->folderPath = path;
    projectName = name;
}

void Project::setProjectGuid(const QString &guid)
{
    this->guid = guid;
}

bool Project::isSaved()
{
    return _saved;
}

QString Project::getProjectName()
{
    return projectName;
}

QString Project::getProjectFolder()
{
    return folderPath;
}

QString Project::getProjectGuid()
{
    return guid;
}

// TODO - repurpose this and all paths
Project* Project::createNew()
{
    auto project = new Project;
    // TODO - change this! overwriting in mainwindow for now
    project->folderPath = QDir::currentPath();
    project->projectName = "Untitled";

    return project;
}
