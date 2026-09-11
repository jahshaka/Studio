/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "data/project.h"

// ONE ENTRY PER ModelTypes VALUE, IN ORDER. It was thirteen entries against a
// seventeen-value enum, and its one consumer indexes it with the enum
// (SceneEditService::exportNodeTo writes the .jaf manifest) — so exporting an
// IES profile, an avatar or an animation clip read PAST THE END of a
// QStringList. Latent only because the export path is reached with Object and
// Mesh; found while appending LiveTexture, fixed rather than carried.
// The existing spellings are a FILE FORMAT (the .jaf manifest) and are
// untouched, including "cubemap" for Sky.
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
    "particle_system",
    "light_profile",
    "avatar",
    "animation",
    "live_texture"
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

// THE STARTUP PLACEHOLDER: no guid and NO FOLDER. A real project's folder is
// always set together with its guid, under the projects root
// (AppPaths::projectsRoot — the data root when a run forces one:
// ProjectService::createProjectShell / pointAtProject, ProjectManager's open,
// import and new-project paths). This one used to be QDir::currentPath() —
// "overwriting in mainwindow for now", which never happened — so everything
// that wrote into "the project folder" before a project existed wrote into
// the process's working directory: the Tile.png beside the binary and in every
// suite's run/ directory was the default scene's copy landing there (plan item
// 15c; the copy itself is gone too). Empty is the honest answer — but note that
// QDir("") IS the working directory, so a writer must still check for an open
// project (getProjectGuid()) before it writes, as the default scene, the
// particle and the material-preset paths do.
Project* Project::createNew()
{
    auto project = new Project;
    project->projectName = "Untitled";
    return project;
}
