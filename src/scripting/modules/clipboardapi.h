/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_CLIPBOARDAPI_H
#define SCRIPTING_CLIPBOARDAPI_H

// clipboard.* — the deep clipboard (CLIPBOARD_SPEC §5, decision D9: its own
// module, because the Assets page, the Materials page, the Timeline and the
// viewport are all callers of the same component and none of them is "editor").
//
// Every surface in the app goes through these verbs: the Ctrl+C/Ctrl+X/Ctrl+V
// chords, the tree's context menu, and anything Claude drives over MCP. The
// older `editor.copy/paste/clipboard` verbs survive as thin aliases (marked
// deprecated in their doc strings) so scripts written against them keep
// working; they now share this one clipboard.

#include <QList>
#include <QVariantList>
#include <QVariantMap>

#include "irisgl/irisglfwd.h"

#include "scripting/apimodule.h"

class ClipboardApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("clipboard"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantMap copy(const QVariant &ids = QVariant());
    Q_INVOKABLE QVariantMap cut(const QVariant &ids = QVariant());
    Q_INVOKABLE QVariantMap copyAssets(const QVariant &guids);
    Q_INVOKABLE QVariantMap paste(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap contents();
    Q_INVOKABLE QString text();
    Q_INVOKABLE QVariantMap setText(const QString &payload);
    Q_INVOKABLE QVariantMap resolve();

private:
    /// The nodes a verb acts on: an explicit id / [id] list, or the selection
    /// when none was given (the chords' behaviour, and the reason the verbs
    /// take an optional argument at all).
    bool resolveNodes(const QVariant &ids, const QString &verb,
                      QList<iris::SceneNodePtr> &out);
    class ClipboardService *service();
};

#endif // SCRIPTING_CLIPBOARDAPI_H
