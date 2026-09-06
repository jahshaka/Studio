/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERAPI_H
#define PLAYERAPI_H

// player.* — the Player space's verbs (verb-coverage audit F1).
//
// The Player page shipped with a verb surface of exactly zero: one play button
// in PlayerWidget, reachable by a human hand and by nothing else. So the space
// the product calls its runtime could not be started by a script, asserted by a
// suite or looked at by an MCP session — and the second engine Scene it runs
// (its own mirror, its own camera, its own PlayBack) had no way to be compared
// against the editor's, which is the comparison every "it looks different in
// the player" report needs.
//
// Every verb here goes through PlayerService (services/playerservice.h), never
// through the widget — the same seam editor.play has in PlaybackService. The
// widget follows the service's signal, so a scripted play moves the button.
//
// WHY player.* AND NOT editor.*: editor.play() is play-IN-PLACE, the editor
// viewport running the scene where it stands. This is the other space. They can
// be in different states at the same time and reporting either through the
// other's verb would be a lie.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class PlayerApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("player"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool play();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool playing();
    Q_INVOKABLE bool restart();
    Q_INVOKABLE QVariantMap state();
    Q_INVOKABLE bool frame(int count = 1, double dt = -1.0);
    Q_INVOKABLE QVariantMap screenshot(const QString &path, const QVariantMap &options = QVariantMap());

private:
    /// The service, or null with a JS error already thrown.
    class PlayerService *serviceOrFail(const char *verb);
};

#endif // PLAYERAPI_H
