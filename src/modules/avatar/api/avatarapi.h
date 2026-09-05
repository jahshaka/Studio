/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARAPI_H
#define AVATARAPI_H

// avatar.* — the Avatar module's verbs (AVATAR_MODULE_SPEC §0.10).
//
// Every verb but `snapshot` drives AvatarPreviewModel, which has no engine in
// it, so the whole surface runs under QT_QPA_PLATFORM=offscreen with no engine
// at all — that is what makes the module API-first-testable. `snapshot` needs
// the bridge's offscreen render, injected as a delegate by AvatarModule when
// the engine is up.
//
// Part 0 is a VIEWER: nothing here touches the library, the database, the
// project or the editor scene's clock. avatar.spawn/list/info/setClipRole are
// Part 1's.

#include <QImage>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "scripting/apimodule.h"

namespace avatar { class AvatarPreviewModel; }

class AvatarApi : public ApiModule
{
    Q_OBJECT
public:
    AvatarApi(ScriptHost &host, avatar::AvatarPreviewModel *model, QObject *parent = nullptr);

    QString jsName() const override { return QStringLiteral("avatar"); }
    QVector<VerbInfo> verbs() const override;

    /// Offscreen render of the module's preview scene (bridge-side). Unset in
    /// headless hosts: avatar.snapshot then fails cleanly instead of crashing.
    using SnapshotFn = std::function<QImage(int, int)>;
    void setSnapshotDelegate(SnapshotFn fn) { mSnapshot = std::move(fn); }
    /// Makes the engine evaluate the current clip time before a pose is read.
    /// Injected by AvatarModule when the engine is up; unset in headless hosts,
    /// where `bones()` reports the rig's shape at its REST pose (documented in
    /// the verb, and why `bones` is Needs::Engine).
    void setPoseResolver(std::function<void()> fn) { mResolvePose = std::move(fn); }
    /// The module persists the space-mode choice; the verb reports through this.
    void setPersistModeDelegate(std::function<void(const char *)> fn) { mPersistMode = std::move(fn); }
    /// Called after any verb that changes what the page shows, so the widgets
    /// follow scripted state (the materials module's selection-delegate shape).
    void setChangedDelegate(std::function<void()> fn) { mChanged = std::move(fn); }
    /// Called ONLY when the subject itself changed (load/clear) — that is when
    /// the camera re-frames. Re-framing on every setTime would fight the orbit.
    void setSubjectDelegate(std::function<void()> fn) { mSubjectChanged = std::move(fn); }

    Q_INVOKABLE QVariant loadPreview(const QString &path);
    Q_INVOKABLE QVariant loadAnimation(const QString &path);
    Q_INVOKABLE bool clearPreview();
    Q_INVOKABLE QVariantList history();
    Q_INVOKABLE bool forget(const QString &path);
    Q_INVOKABLE bool setRootMotion(bool on);
    Q_INVOKABLE QVariant spaceMode(const QVariant &mode = QVariant());

    /// The message of the last verb failure, for the widgets. ApiModule::fail
    /// throws into the JS engine, which a button click has no access to — the
    /// page still has to be able to show a rig-mismatch refusal to the user.
    QString lastError() const { return mLastError; }
    Q_INVOKABLE QVariant preview();
    Q_INVOKABLE bool setMeshVisible(bool on);
    Q_INVOKABLE bool setSkeletonVisible(bool on);
    Q_INVOKABLE QVariantList clips();
    Q_INVOKABLE bool playClip(const QString &name = QString());
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool setClip(const QString &name);
    Q_INVOKABLE bool setLooping(bool on);
    Q_INVOKABLE bool setTime(double seconds);
    Q_INVOKABLE double time();
    Q_INVOKABLE QVariantList bones();
    /// The module's built-in head/shoulder sockets, on an editor-scene node
    /// (CAMERAS_SPEC D9). The only verb here that leaves the preview document.
    Q_INVOKABLE QVariantList addSockets(const QString &nodeId);
    Q_INVOKABLE QVariantMap snapshot(const QString &path, int width = 256, int height = 256,
                                     const QVariantList &probes = QVariantList());

private:
    QVariantMap previewState() const;
    void notifyChanged();
    void notifySubjectChanged();
    /// fail(), plus a copy of the message the widgets can read back.
    bool record(const QString &message);

    QString mLastError;

    avatar::AvatarPreviewModel *mModel = nullptr;
    SnapshotFn mSnapshot;
    std::function<void()> mResolvePose;
    std::function<void(const char *)> mPersistMode;
    std::function<void()> mChanged;
    std::function<void()> mSubjectChanged;
};

#endif // AVATARAPI_H
