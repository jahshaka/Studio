/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/exportservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "export/gltfexporter.h"

namespace {

QString readResource(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(f.readAll());
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(bytes) == bytes.size();
}

QString buildViewerHtml(const QString &tpl, const QString &bundle, const QString &viewerJs,
                        const QString &title, const QJsonObject &config)
{
    QString html = tpl;
    // replace() with plain strings — the payloads contain regex/backreference
    // metacharacters, so no QRegularExpression anywhere near this.
    html.replace(QStringLiteral("<!--JAH_TITLE-->"), title.toHtmlEscaped());
    html.replace(QStringLiteral("/*JAH_CONFIG*/"),
                 QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)));
    html.replace(QStringLiteral("/*JAH_BUNDLE*/"), bundle);
    html.replace(QStringLiteral("/*JAH_VIEWER*/"), viewerJs);
    return html;
}

QByteArray readmeText(const QString &sceneName, bool inlined)
{
    QString t;
    t += QStringLiteral("%1 — Jahshaka web export\n").arg(sceneName);
    t += QStringLiteral("=========================================\n\n");
    if (inlined) {
        t += QStringLiteral(
            "index.html   Double-click it. Fully self-contained (viewer + scene embedded);\n"
            "             needs a WebGPU browser: Chrome/Edge 113+, Firefox 141+ (Windows),\n"
            "             Safari 26+. No server, no internet.\n\n");
    } else {
        t += QStringLiteral(
            "This scene was too large to embed into a single file, so index.html only\n"
            "carries a notice. Use the served path below.\n\n");
    }
    t += QStringLiteral(
        "viewer.html  The same viewer loading scene.glb from disk. Browsers refuse\n"
        "             file:// fetches, so serve the folder first:\n"
        "                 python3 -m http.server\n"
        "             then open http://localhost:8000/viewer.html\n\n"
        "scene.glb    The scene as standard binary glTF 2.0 — usable in any glTF\n"
        "             tool (Blender, three.js editor, gltf-viewer.donmccurdy.com).\n\n"
        "NOT EXPORTED\n"
        "------------\n"
        "Planar reflections (objects marked \"Planar Reflector\" in the editor) are\n"
        "recorded in the file as extras.jah.planarReflector, but the viewer ignores\n"
        "them: those surfaces render with their ordinary material.\n\n"
        "LICENSES\n"
        "--------\n"
        "The embedded viewer runtime is three.js (https://threejs.org), MIT License,\n"
        "Copyright (c) 2010-2026 three.js authors. The scene content is yours.\n");
    return t.toUtf8();
}

} // namespace

ExportService::WebExportResult ExportService::exportWeb(const iris::ScenePtr &scene,
                                                        const QString &sceneName,
                                                        const QString &outDir)
{
    WebExportResult r;
    if (outDir.trimmed().isEmpty()) { r.error = QStringLiteral("no output directory given"); return r; }

    QDir dir(outDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        r.error = QStringLiteral("could not create output directory: %1").arg(outDir);
        return r;
    }

    const QString title = sceneName.isEmpty() ? QStringLiteral("Jahshaka Scene") : sceneName;
    GltfExporter::Result g = GltfExporter::exportScene(scene, title);
    if (!g.ok) { r.error = g.error; return r; }

    r.dir = dir.absolutePath();
    r.warnings = g.warnings;
    r.extensionsUsed = g.extensionsUsed;
    r.nodeCount = g.nodeCount;
    r.meshCount = g.meshCount;
    r.materialCount = g.materialCount;
    r.lightCount = g.lightCount;
    r.cameraCount = g.cameraCount;
    r.animationCount = g.animationCount;

    r.glbPath = dir.filePath(QStringLiteral("scene.glb"));
    if (!writeFile(r.glbPath, g.glb)) {
        r.error = QStringLiteral("could not write %1").arg(r.glbPath);
        return r;
    }
    r.glbSize = g.glb.size();

    const QString tpl = readResource(QStringLiteral(":/export/index_template.html"));
    const QString bundle = readResource(QStringLiteral(":/export/three-webgpu.iife.js"));
    const QString viewerJs = readResource(QStringLiteral(":/export/viewer.js"));
    if (tpl.isEmpty() || bundle.isEmpty() || viewerJs.isEmpty()) {
        r.error = QStringLiteral("viewer resources missing from the app build (:/export/*)");
        return r;
    }

    // index.html — embedded (base64) unless past the size ceiling (audit §3).
    r.inlined = g.glb.size() <= kInlineCeilingBytes;
    if (!r.inlined)
        r.warnings.append(QStringLiteral("scene is %1 MB — too large to embed; index.html "
                                         "redirects to the served viewer.html path")
                              .arg(g.glb.size() / (1024 * 1024)));
    else if (g.glb.size() > 25ll * 1024 * 1024)
        r.warnings.append(QStringLiteral("scene is %1 MB — index.html will take seconds to parse")
                              .arg(g.glb.size() / (1024 * 1024)));

    QJsonObject cfgIndex;
    cfgIndex["title"] = title;
    if (r.inlined) cfgIndex["glbBase64"] = QString::fromLatin1(g.glb.toBase64());
    else cfgIndex["notInlined"] = true;
    const QString indexHtml = buildViewerHtml(tpl, bundle, viewerJs, title, cfgIndex);
    r.indexHtml = dir.filePath(QStringLiteral("index.html"));
    if (!writeFile(r.indexHtml, indexHtml.toUtf8())) {
        r.error = QStringLiteral("could not write %1").arg(r.indexHtml);
        return r;
    }
    r.indexSize = QFileInfo(r.indexHtml).size();

    // viewer.html — same page, fetch path (served layout).
    QJsonObject cfgViewer;
    cfgViewer["title"] = title;
    const QString viewerHtml = buildViewerHtml(tpl, bundle, viewerJs, title, cfgViewer);
    r.viewerHtml = dir.filePath(QStringLiteral("viewer.html"));
    if (!writeFile(r.viewerHtml, viewerHtml.toUtf8())) {
        r.error = QStringLiteral("could not write %1").arg(r.viewerHtml);
        return r;
    }

    writeFile(dir.filePath(QStringLiteral("README.txt")), readmeText(title, r.inlined));

    // ambient audio: copy beside the export for the (future) served viewer.
    if (!g.audioSourcePath.isEmpty() && QFileInfo::exists(g.audioSourcePath)) {
        const QString dst = dir.filePath(QFileInfo(g.audioSourcePath).fileName());
        if (!QFile::exists(dst)) QFile::copy(g.audioSourcePath, dst);
    }

    r.ok = true;
    return r;
}
