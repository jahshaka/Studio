/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/projectapi.h"

#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QStandardPaths>

#include "export/exportservice.h"
#include "export/previewlauncher.h"
#include "services/sceneeditservice.h"

#include "data/database/database.h"
#include "data/project.h"
#include "shell/mainwindow.h"
#include "services/projectservice.h"
#include "services/services.h"
#include "ui/pages/projectmanager.h"

QVector<VerbInfo> ProjectApi::verbs() const
{
    return {
        { "create", "project.create(name) -> guid",
          "Creates a project (folder, DB row, default scene saved into the blob) on the current desktop and opens it in the editor.",
          Needs::Document },
        { "open", "project.open(guidOrName) -> bool",
          "Opens a project by guid or exact name: preloads its assets synchronously, reads the scene blob, switches to the editor.",
          Needs::Document },
        { "save", "project.save() -> bool",
          "Saves the open scene into the project's DB blob. Works headless (blob-only; the thumbnail refreshes only when a viewport can render one).",
          Needs::Document },
        { "close", "project.close() -> bool",
          "Closes the open project (physics restored, autosave per settings, undo stack reset) and returns to the desktop.",
          Needs::Document },
        { "rename", "project.rename(guid, newName) -> bool",
          "Renames a project in the database.",
          Needs::Document },
        { "remove", "project.remove(guid) -> bool",
          "Deletes a project: its folder tree, its DB row and its asset/dependency rows. Refuses to delete the open project. NOT undoable.",
          Needs::Document },
        { "list", "project.list({desktop}) -> [{guid, name, desktop, x, y}]",
          "Lists projects; desktop 1-4 filters, omit for all.",
          Needs::Document },
        { "moveToDesktop", "project.moveToDesktop(guid, desktop) -> bool",
          "Moves a project tile to desktop 1-4.",
          Needs::Document },
        { "setPosition", "project.setPosition(guid, x, y) -> bool",
          "Sets a tile's freeform position (normalized 0..1) on its desktop.",
          Needs::Document },
        { "current", "project.current() -> {guid, name, folder} | null",
          "The open project, or null.",
          Needs::Document },
        { "exportWeb", "project.exportWeb(dir) -> {dir, indexHtml, glb, nodes, materials, extensions, warnings, ...}",
          "Exports the open scene for the web (glTF 2.0 + self-contained WebGPU viewer): index.html (double-clickable), "
          "viewer.html + scene.glb (served path), README.txt. dir defaults to <project>/exports/web. Document-only; works headless.",
          Needs::Document },
        { "previewWeb", "project.previewWeb(dir) -> {browser, mode}",
          "Opens an existing web export (see exportWeb) in a Chromium-family browser as a chromeless --app window, "
          "or the default browser when none is found. mode is 'kiosk' or 'browser'.",
          Needs::Document },
    };
}

QString ProjectApi::resolveGuid(const QString &guidOrName, QString *nameOut)
{
    int hits = 0;
    const QString guid =
        host.services->project->resolveProjectGuid(guidOrName, nameOut, &hits);
    if (hits > 1) {
        fail(QStringLiteral("project: name '%1' matches %2 projects — use the guid").arg(guidOrName).arg(hits));
        return QString();
    }
    return guid;
}

QString ProjectApi::create(const QString &name)
{
    if (!host.mainWindow || !host.services || !host.services->project) { fail("project: not available in this session"); return QString(); }
    if (name.trimmed().isEmpty()) { fail("project.create: a non-empty name is required"); return QString(); }

    // The data half (guid, current project, folder, DB row, desktop) is
    // ProjectService's; MainWindow::newProject then builds the default scene
    // and saves it, so the row never carries the empty scene blob (the crash
    // window the census flagged).
    const QString guid = host.services->project->createProjectShell(name);
    if (guid.isEmpty()) {
        fail("project.create: the database rejected the project row");
        return QString();
    }

    host.mainWindow->newProject(name.trimmed(), host.project->getProjectFolder());
    return guid;
}

bool ProjectApi::open(const QString &guidOrName)
{
    if (!host.mainWindow || !host.services || !host.services->project)
        return fail("project: not available in this session");

    QString name;
    const QString guid = resolveGuid(guidOrName, &name);
    if (guid.isEmpty()) {
        // resolveGuid already threw for an ambiguous name; add the not-found case.
        if (QJSEngine *js = qjsEngine(this); !js || !js->hasError())
            fail(QStringLiteral("project.open: no project named or guid '%1'").arg(guidOrName));
        return false;
    }

    if (host.project->getProjectGuid() == guid && host.services->project->isSceneOpen()) {
        host.mainWindow->switchSpace(WindowSpaces::EDITOR);
        return true;
    }
    if (host.services->project->isSceneOpen()) host.mainWindow->closeProject();

    // Point the current project + synchronous preload, then the reader half.
    host.services->project->prepareOpen(guid, name);
    host.mainWindow->openProject(false);
    return true;
}

bool ProjectApi::save()
{
    if (!requireProject()) return false;
    if (!host.services || !host.services->project || !host.services->project->saveProjectBlob())
        return fail("project.save: the blob save failed");
    return true;
}

bool ProjectApi::close()
{
    if (!requireProject()) return false;
    host.mainWindow->closeProject();
    return true;
}

bool ProjectApi::rename(const QString &guid, const QString &newName)
{
    if (!host.db) return fail("project: not available in this session");
    if (newName.trimmed().isEmpty()) return fail("project.rename: a non-empty name is required");
    if (!host.db->renameProject(guid, newName.trimmed()))
        return fail(QStringLiteral("project.rename: no project with guid '%1'").arg(guid));
    if (host.project->getProjectGuid() == guid)
        host.project->setProjectPath(host.project->getProjectFolder(), newName.trimmed());
    return true;
}

bool ProjectApi::remove(const QString &guid)
{
    if (!host.db || !host.services || !host.services->project)
        return fail("project: not available in this session");
    if (host.services->project->isSceneOpen() && host.project->getProjectGuid() == guid)
        return fail("project.remove: this project is open — project.close() first");

    QString name;
    if (resolveGuid(guid, &name) != guid)
        return fail(QStringLiteral("project.remove: no project with guid '%1'").arg(guid));

    // Folder first (like the widget), then the DB rows — through the
    // guid-parameterised service: host.project is NOT mutated (§1.6.1).
    if (!host.services->project->removeProject(guid))
        return fail("project.remove: could not remove the project folder");
    return true;
}

QVariantList ProjectApi::list(const QVariantMap &options)
{
    QVariantList out;
    if (!host.db) { fail("project: not available in this session"); return out; }
    const int desktop = options.value("desktop", 0).toInt();
    for (const auto &p : host.db->fetchProjects(desktop)) {
        QVariantMap m;
        m["guid"] = p.guid;
        m["name"] = p.name;
        m["desktop"] = p.desktop;
        if (p.hasPosition) { m["x"] = p.posX; m["y"] = p.posY; }
        out.append(m);
    }
    return out;
}

bool ProjectApi::moveToDesktop(const QString &guid, int desktop)
{
    if (!host.db) return fail("project: not available in this session");
    if (desktop < 1 || desktop > 4) return fail("project.moveToDesktop: desktop must be 1-4");
    if (!host.db->updateProjectDesktop(guid, desktop))
        return fail(QStringLiteral("project.moveToDesktop: no project with guid '%1'").arg(guid));
    if (host.projectManager) host.projectManager->populateDesktop(true);
    return true;
}

bool ProjectApi::setPosition(const QString &guid, double x, double y)
{
    if (!host.db) return fail("project: not available in this session");
    if (!host.db->updateProjectPosition(guid, float(x), float(y)))
        return fail(QStringLiteral("project.setPosition: no project with guid '%1'").arg(guid));
    return true;
}

QVariantMap ProjectApi::exportWeb(const QString &dir)
{
    QVariantMap out;
    if (!requireProject()) return out;
    if (!host.services || !host.services->sceneEdit) { fail("project.exportWeb: no scene service"); return out; }
    auto scene = host.services->sceneEdit->scene();
    if (!scene) { fail("project.exportWeb: no scene is open"); return out; }

    QString outDir = dir.trimmed();
    if (outDir.isEmpty())
        outDir = QDir(host.project->getProjectFolder()).filePath(QStringLiteral("exports/web"));

    const auto r = ExportService::exportWeb(scene, host.project->getProjectName(), outDir);
    if (!r.ok) { fail(QStringLiteral("project.exportWeb: %1").arg(r.error)); return out; }

    out["dir"] = r.dir;
    out["indexHtml"] = r.indexHtml;
    out["viewerHtml"] = r.viewerHtml;
    out["glb"] = r.glbPath;
    out["glbSize"] = r.glbSize;
    out["indexSize"] = r.indexSize;
    out["inlined"] = r.inlined;
    out["nodes"] = r.nodeCount;
    out["meshes"] = r.meshCount;
    out["materials"] = r.materialCount;
    out["lights"] = r.lightCount;
    out["cameras"] = r.cameraCount;
    out["animations"] = r.animationCount;
    out["extensions"] = QVariant(r.extensionsUsed);
    out["warnings"] = QVariant(r.warnings);
    return out;
}

QVariantMap ProjectApi::previewWeb(const QString &dir)
{
    QVariantMap out;
    if (!requireProject()) return out;

    QString outDir = dir.trimmed();
    if (outDir.isEmpty())
        outDir = QDir(host.project->getProjectFolder()).filePath(QStringLiteral("exports/web"));
    const QString indexHtml = QDir(outDir).filePath(QStringLiteral("index.html"));
    if (!QFileInfo::exists(indexHtml)) {
        fail(QStringLiteral("project.previewWeb: no export at %1 — project.exportWeb() first").arg(indexHtml));
        return out;
    }

    const QString browser = PreviewLauncher::findChromiumBrowser();
    if (!browser.isEmpty() && PreviewLauncher::launchKiosk(indexHtml, this)) {
        out["browser"] = browser;
        out["mode"] = "kiosk";
        return out;
    }
    if (!PreviewLauncher::openInBrowser(indexHtml)) {
        fail("project.previewWeb: no browser could be launched");
        return out;
    }
    out["browser"] = QStringLiteral("default");
    out["mode"] = "browser";
    return out;
}

QVariant ProjectApi::current()
{
    if (!host.isProjectOpen()) return QVariant();
    QVariantMap m;
    m["guid"] = host.project->getProjectGuid();
    m["name"] = host.project->getProjectName();
    m["folder"] = host.project->getProjectFolder();
    return m;
}
