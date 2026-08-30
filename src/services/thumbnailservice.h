/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef THUMBNAILSERVICE_H
#define THUMBNAILSERVICE_H

// ThumbnailService — asset thumbnail requests (APP_ARCHITECTURE_AUDIT §3.3).
//
// The DB-lookup half of MainWindow::refreshThumbnail: resolve an object
// asset's mesh file and queue a thumbnail render on the ThumbnailGenerator
// façade (post-step-14 that façade becomes a thin queue over
// EngineThumbnailRenderer — audit §5.5; this service is its call site).
// QObject-free.

#include <QString>

class Database;
class Project;

class ThumbnailService
{
public:
    ThumbnailService(Database *db, Project *project);

    /// Requests a fresh thumbnail for a stored object asset (guid names the
    /// Object row; its mesh file is resolved through the dependency rows).
    void refreshObjectThumbnail(const QString &guid);

private:
    Database *db;
    Project *project;
};

#endif // THUMBNAILSERVICE_H
