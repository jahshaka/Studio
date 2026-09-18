/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "bridge/assetthumbnail.h"

#include <QDebug>
#include <QPixmap>

#include "bridge/enginethumbnailrenderer.h"
#include "data/database/database.h"
#include "services/assethelper.h"
#include "services/libraryassetnode.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace assetthumb
{

namespace
{
QImage say(QString *reasonOut, const QString &why)
{
    if (reasonOut) *reasonOut = why;
    qWarning("thumbnail: %s", qUtf8Printable(why));
    return QImage();
}
}   // namespace

QImage renderObject(Database *db, Project *project, const QString &guid,
                    const std::shared_ptr<jahshaka::engine::Engine> &engine, QSize size,
                    QString *reasonOut, bool *noModelOut)
{
    if (reasonOut) reasonOut->clear();
    if (noModelOut) *noModelOut = false;
    if (!db) return say(reasonOut, QStringLiteral("there is no library in this session"));
    if (guid.isEmpty()) return say(reasonOut, QStringLiteral("no asset guid was given"));
    if (!engine)
        return say(reasonOut, QStringLiteral("the engine is not running (a model thumbnail is a render)"));
    iris::SceneNodePtr node = libraryasset::fromLibrary(db, project, guid);
    if (!node) {
        if (noModelOut) *noModelOut = true;
        return say(reasonOut, QStringLiteral("the stored model for '%1' could not be read "
                                             "(no blob, or a blob with no geometry)").arg(guid));
    }

    // THE ONE RENDERER, BORROWED (THUMBS-1). This used to CONSTRUCT one, which
    // is why an import after the session's first material thumbnail produced a
    // grey tile: the engine refused the duplicate View name and the null image
    // travelled all the way to the database.
    auto loan = EngineThumbnailRenderer::borrow(engine, "the model thumbnail");
    if (!loan) return say(reasonOut, loan.reason());
    const QImage image = loan->renderNode(node, size);
    if (image.isNull()) return say(reasonOut, loan->lastFailure());
    return image;
}

bool storeObject(Database *db, Project *project, const QString &guid,
                 const std::shared_ptr<jahshaka::engine::Engine> &engine, QSize size,
                 QString *reasonOut, bool *noModelOut)
{
    const QImage image = renderObject(db, project, guid, engine, size, reasonOut, noModelOut);
    if (image.isNull()) return false;
    if (!db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(image)))) {
        if (reasonOut) *reasonOut = QStringLiteral("the rendered thumbnail could not be written to the library");
        return false;
    }
    return true;
}

}   // namespace assetthumb
