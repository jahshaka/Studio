/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXPORTSERVICE_H
#define EXPORTSERVICE_H

// ExportService — web export orchestration (WEB_EXPORT_AUDIT §2/§3): runs the
// GltfExporter over the live document scene and writes the export folder:
//
//   index.html   fully self-contained (three.js WebGPU IIFE bundle + viewer +
//                GLB as base64) — double-click to view, works from file://
//   viewer.html  the same viewer fetching scene.glb (the served path)
//   scene.glb    the scene as a standalone GLB
//   README.txt   how to serve big scenes + the three.js MIT notice
//
// Size ceiling (audit §3): assets > ~75 MB are not inlined — index.html then
// carries a "serve this folder" notice instead of the scene.
//
// Both the `project.exportWeb` verb and the Publish panel call exportWeb() —
// one seam, per the API-first rule.

#include <QString>
#include <QStringList>

#include "irisgl/irisglfwd.h"

class ExportService
{
public:
    struct WebExportResult
    {
        bool ok = false;
        QString error;
        QString dir;
        QString indexHtml;
        QString viewerHtml;
        QString glbPath;
        qint64 glbSize = 0;
        qint64 indexSize = 0;
        bool inlined = false;        // false = size ceiling hit, served layout only
        QStringList warnings;
        QStringList extensionsUsed;
        int nodeCount = 0;
        int meshCount = 0;
        int materialCount = 0;
        int lightCount = 0;
        int cameraCount = 0;
        int animationCount = 0;
    };

    /// Exports `scene` into `outDir` (created if missing). `sceneName` titles
    /// the viewer page. Pure document consumer — safe headless.
    static WebExportResult exportWeb(const iris::ScenePtr &scene,
                                     const QString &sceneName,
                                     const QString &outDir);

    /// GLB payloads above this refuse to inline into index.html (audit §3).
    static constexpr qint64 kInlineCeilingBytes = 75ll * 1024 * 1024;
};

#endif // EXPORTSERVICE_H
