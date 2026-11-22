/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_PREVIEWSAPI_H
#define SCRIPTING_PREVIEWSAPI_H

// previews.* — THE STORED PREVIEWS of the library's rows (PREVIEWS-1; services/previewservice.h).
//
// state() is a comparison of keys, CPU only, headless: what a preview depicts against what the
// stored one was drawn from. render() and renderAll() draw through the one service and WAIT for the
// write to land - the one wait of the preview path, in these synchronous verbs' door only, bounded
// (PICTURE-REQUEST-1 stage 3 replaces it by a ticket); settle() waits for every write the writer
// holds, whoever asked (an import's pictures). They replace assets.refreshThumbnail and
// assets.rebuildThumbnails.

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class PreviewsApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("previews"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantList state(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap render(const QString &guid);
    Q_INVOKABLE QVariantMap renderAll(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap settle();
};

#endif // SCRIPTING_PREVIEWSAPI_H
