/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRMODULE_H
#define VRMODULE_H

// VrModule — the VR feature domain (SPECS/VR_SPEC.md §4.6, phase 2).
//
// A StudioModule with NO PAGE, deliberately: phase 2 delivers the session and
// its verbs, and the UI is phase 3's (the Player's VR mode) and phase 4's (the
// editor preview). New feature domains land as modules rather than MainWindow
// wiring, and a module whose whole interface today is five verbs is still a
// module — that is what makes the UI, when it comes, a caller of the same verbs
// instead of a second path into the engine.
//
// It also owns the session's LIFETIME against the shell's: `shutdown()` ends a
// running session, because a session outliving the editor's scene would hold a
// View and a workspace on a target that is about to die.

#include <QPointer>
#include <QVariantMap>

#include <memory>

#include "modules/studiomodule.h"

class QAction;
struct ScriptHost;
class VrApi;
class QMessageBox;

class VrModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("vr"); }
    VrModule();
    ~VrModule() override;
    void initialize(StudioContext &host) override { this->host = host; }
    /// THE VR TOGGLE (SPECS/VR_SPEC.md §4.5): the editor toolbar's action (its
    /// `editor.vr` slot) and the vr.toggle chord. The Player page's own button
    /// fires the same row, so every surface ends in toggle().
    void contribute(Contributions &c) override;
    void registerApi(ScriptEngine &engine) override;
    void shutdown() override;

    /// THE BUTTON MEANS "VR, HERE" (the owner, 2026-09-17, at the first
    /// controller smoke: "the VR button takes me to the Player"): on the editor
    /// page it starts (or ends) the EDITOR PREVIEW — vr.begin / vr.end, the
    /// headset as a live window on the editor; on any other page it is the
    /// Player's run in the headset — PlayerService::toggleVr, what `vr.toggle()`
    /// calls. Both are the verbs' own paths, never a second one.
    void toggle();

    /// THE VR START REPORT (VR-START-1): the last start failure this process
    /// told the user about — {failure, reason, title, text, dialogs, notices,
    /// dialogOpen} — so `vr.startReport()` can see what the user saw.
    QVariantMap startReport() const;
    /// The failure dialog's primary action, exactly as its button runs it:
    /// close the dialog and press the VR button again. False when no dialog
    /// is open.
    bool tryAgain();

private:
    /// THE STARTUP HEADSET CHECK (Start in VR, --vr): once the window is up,
    /// ask the engine for a probe; on a miss, the notice (the preference's
    /// check) or the dialog (an explicit --vr).
    void scheduleStartCheck();
    void runStartCheck();
    /// EVERY VR START FAILURE ENDS HERE (VR-START-1 §1): the desktop stays the
    /// editor and the user gets ONE clear message with an action — the dialog
    /// (explicit asks: the button, --vr, a dropped session) with Try again, or
    /// the non-modal notice (the Start in VR check). `fallbackReason` is the
    /// refusal's own words when the engine has no failure class for it.
    void reportStartFailure(bool dialog, const QString &fallbackReason = QString());
    QPointer<QMessageBox> mStartDialog;

    bool toggleEditorPreview();
    bool isEditorPreviewActive() const;
    /// Icon + tooltip + enabled state of the VR action, from the live session.
    void refreshUi();

    StudioContext host;
    ScriptHost *scriptHost = nullptr;
    std::unique_ptr<QAction> mAction;
    /// What the icon is currently showing, so the per-frame refresh only
    /// rebuilds when the answer moves.
    bool mIconActive = false;
    /// May this PROCESS attempt VR at all (the policy: not --no-vr /
    /// JAHSHAKA_VR=0, not headless)? Fixed for the process, so the per-frame
    /// follower reads this cached bool first and a VR-off run pays nothing.
    bool mCapable = false;
    /// The module's ApiModule, owned by the ScriptEngine — held weakly so
    /// shutdown() can end a session through the object that owns it.
    QPointer<VrApi> api;
};

#endif // VRMODULE_H
