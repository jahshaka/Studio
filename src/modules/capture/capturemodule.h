/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CAPTUREMODULE_H
#define CAPTUREMODULE_H

// CaptureModule — the VIDEO RECORDER's feature domain (VIDEO-REC-1;
// SPECS/VIDEO_CAPTURE_SPEC.md §2 + the owner's answers in §10).
//
// A StudioModule with NO PAGE, in VrModule's shape: its whole interface is the
// `capture.*` verbs (CaptureApi), and its one piece of UI — the RECORD BUTTON
// beside the photo button, the toolbar slot `editor.capture` — calls those
// verbs' own implementation, never a second path (API-first).
//
// THE BUTTON (§10.6): press to record, press again to stop. While recording it
// is RED with the elapsed time of the video; there is NO toast. Its popup holds
// the scene-only/helpers switch (§10.5, persisted `capture/helpers`) and, once a
// recording has finished, the file and "Open folder" (the tooltip names it too).
// Esc stops a recording from the viewport (IEditorViewport::RecordingHooks).
// A failure is said once, in a dialog with the reason (the VR rule), never a
// crash and never a silent no-file.

#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <memory>

#include "modules/studiomodule.h"

class CaptureApi;
class QAction;
class QMenu;
class QMessageBox;
class QToolButton;
class VideoRecorder;

class CaptureModule : public StudioModule
{
public:
    CaptureModule();
    ~CaptureModule() override;
    QString id() const override { return QStringLiteral("capture"); }
    void initialize(StudioContext &ctx) override;
    void contribute(Contributions &c) override;
    void registerApi(ScriptEngine &engine) override;
    void onProjectChanged(Project *project) override;
    void abortBackgroundWork() override;
    void shutdown() override;

    /// Null in a host with no editor viewport (headless): every verb refuses.
    VideoRecorder *recorder() const { return mRecorder.get(); }

    /// THE BUTTON, exactly as a click runs it: start (with the switch's
    /// helpers answer) when idle, stop when recording. False when the click
    /// did nothing (a refusal is reported the way the button reports it).
    bool press();
    /// What the button is showing: {text, toolTip, recording, red, enabled,
    /// helpers, menu:[row texts], failure, dialogOpen}.
    QVariantMap button() const;
    /// The helpers switch (the popup's row and `capture/helpers`).
    bool helpersSwitch() const;
    void setHelpersSwitch(bool on);
    /// Closes the failure dialog; false when none is open.
    bool dismissFailure();

private:
    void refreshUi();
    void onFinished(bool ok, const QString &path, const QString &error);
    void reportFailure(const QString &message);
    void styleButton();

    StudioContext host;
    std::unique_ptr<VideoRecorder> mRecorder;
    std::unique_ptr<QAction> mAction;
    std::unique_ptr<QMenu> mMenu;
    QAction *mHelpersRow = nullptr;
    QAction *mFileRow = nullptr;
    QAction *mOpenFolderRow = nullptr;
    QPointer<QToolButton> mButton;
    QPointer<QMessageBox> mFailureDialog;
    QPointer<CaptureApi> mApi;
    QString mLastShownText;
    QString mLastFailure;
};

#endif // CAPTUREMODULE_H
