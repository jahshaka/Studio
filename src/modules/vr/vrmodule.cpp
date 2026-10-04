/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrmodule.h"

#include <QAction>
#include <QColor>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include "bridge/enginehost.h"
#include "bridge/vrnames.h"
#include "modules/vr/vrapi.h"
#include "scripting/scriptengine.h"
#include "scripting/scripthost.h"
#include "services/playerservice.h"
#include "services/projectservice.h"
#include "services/services.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "ui/controls/fonticons.h"
#include "ui/ishellview.h"
#include "viewport/enginerenderdriver.h"

VrModule::VrModule() = default;
VrModule::~VrModule() = default;

namespace {
/// THE LAST VR START FAILURE THE USER WAS TOLD ABOUT (VR-START-1), process-wide
/// like the session: what `vr.startReport()` reads.
struct StartRecord {
    QString failure;
    QString reason;
    QString title;
    QString text;
    int dialogs = 0;
    int notices = 0;
};
StartRecord gStart;

jahshaka::engine::Engine *engineOf(const StudioContext &host)
{
    if (!host.engine) return nullptr;
    const auto e = host.engine->engine();
    return e ? e.get() : nullptr;
}
}

QVariantMap VrModule::startReport() const
{
    QVariantMap out;
    out[QStringLiteral("failure")] = gStart.failure;
    out[QStringLiteral("reason")] = gStart.reason;
    out[QStringLiteral("title")] = gStart.title;
    out[QStringLiteral("text")] = gStart.text;
    out[QStringLiteral("dialogs")] = gStart.dialogs;
    out[QStringLiteral("notices")] = gStart.notices;
    out[QStringLiteral("dialogOpen")] = !mStartDialog.isNull() && mStartDialog->isVisible();
    return out;
}

bool VrModule::tryAgain()
{
    if (mStartDialog.isNull() || !mStartDialog->isVisible()) return false;
    mStartDialog->hide();
    toggle();
    return true;
}

void VrModule::reportStartFailure(bool dialog, const QString &fallbackReason)
{
    using jahshaka::engine::VrFailure;
    jahshaka::engine::Engine *e = engineOf(host);
    const jahshaka::engine::VrInfo info = e ? e->vrInfo() : jahshaka::engine::VrInfo();
    const VrFailure f = e ? info.failure : VrFailure::Headless;
    const QString runtime = vrLaunch().anyRuntime || vrLaunch().headsetRuntime.isEmpty()
                                ? QObject::tr("the VR runtime")
                                : vrLaunch().headsetRuntime;
    const QString reason = f == VrFailure::None || info.reason.empty()
                               ? fallbackReason
                               : QString::fromStdString(info.reason);
    QString title, text;
    switch (f) {
    case VrFailure::NoRuntime:
        title = QObject::tr("No headset found");
        text = QObject::tr("No VR runtime is running. Start %1, connect the headset, then click "
                           "Try again.").arg(runtime);
        break;
    case VrFailure::WrongRuntime:
        title = QObject::tr("No headset found");
        text = QObject::tr("The active VR runtime is %1, not %2. Start %2 and connect the headset, "
                           "then click Try again. (Settings > General > Editor chooses the headset "
                           "runtime.)")
                   .arg(QString::fromStdString(info.manifestRuntime.empty() ? info.manifest
                                                                            : info.manifestRuntime),
                        runtime);
        break;
    case VrFailure::NoHeadset:
        title = QObject::tr("No headset found");
        text = QObject::tr("%1 is running but no headset is connected. Connect the headset, then "
                           "click Try again.").arg(runtime);
        break;
    case VrFailure::ConnectionLost:
        title = QObject::tr("The headset disconnected");
        text = QObject::tr("The headset or its runtime went away. The editor is still here: "
                           "reconnect the headset, then click Try again.");
        break;
    case VrFailure::DeviceMismatch:
        title = QObject::tr("VR did not start");
        text = QObject::tr("The VR runtime cannot use the GPU this editor runs on.");
        break;
    case VrFailure::Disabled:
        title = QObject::tr("VR is off");
        text = QObject::tr("VR is off for this run (--no-vr or JAHSHAKA_VR=0).");
        break;
    case VrFailure::Headless:
        title = QObject::tr("VR is off");
        text = QObject::tr("This run has no renderer to share with a headset.");
        break;
    case VrFailure::RuntimeBroken:
    case VrFailure::None:
        title = QObject::tr("VR did not start");
        text = QObject::tr("The VR runtime failed to start a session. Restart %1, then click Try "
                           "again.").arg(runtime);
        break;
    }
    gStart.failure = vrnames::failure(f);
    gStart.reason = reason;
    gStart.title = title;
    gStart.text = text;
    qWarning("Jahshaka VR: %s - %s (%s)", qPrintable(title), qPrintable(reason),
             qPrintable(gStart.failure));
    if (!dialog) {
        ++gStart.notices;
        if (host.shell) host.shell->showNotice(title, text);
        return;
    }
    ++gStart.dialogs;
    if (!host.shellWidget) return;
    // ONE DIALOG, REUSED, NEVER MODAL: a VR failure must not block the editor
    // (nor a script driving it), and a second failure updates the open one
    // rather than stacking a second.
    if (mStartDialog.isNull()) {
        mStartDialog = new QMessageBox(host.shellWidget);
        mStartDialog->setObjectName(QStringLiteral("vrStartDialog"));
        mStartDialog->setIcon(QMessageBox::Warning);
        mStartDialog->setWindowModality(Qt::NonModal);
        QPushButton *again = mStartDialog->addButton(QObject::tr("Try again"),
                                                     QMessageBox::AcceptRole);
        mStartDialog->addButton(QMessageBox::Close);
        mStartDialog->setDefaultButton(again);
        // A QMessageBox button closes the box itself; the retry runs after it,
        // on the same path vr.tryAgain() takes.
        QObject::connect(again, &QPushButton::clicked, mAction.get(), [this]() {
            QTimer::singleShot(0, mAction.get(), [this]() { toggle(); });
        });
    }
    mStartDialog->setWindowTitle(title);
    mStartDialog->setText(text);
    mStartDialog->setInformativeText(reason);
    mStartDialog->show();
    mStartDialog->raise();
}

void VrModule::contribute(Contributions &c)
{
    // One action, beside the camera controls it belongs with: press it and the
    // headset comes up (the editor preview on the editor page, the Player's run
    // elsewhere); press it again and it ends.
    //
    // fa::binoculars is the closest thing the shipped icon font (Font Awesome
    // 4) has to a headset: a two-lens device held to the eyes. Stated because
    // it is a choice, not an obvious match.
    QVariantMap options;
    options.insert("color", QColor(255, 255, 255));
    options.insert("color-active", QColor(255, 255, 255));
    mAction.reset(new QAction);
    mAction->setObjectName(QStringLiteral("actionVr"));
    mAction->setCheckable(true);
    mAction->setIcon(fonticons::shared().icon(fa::binoculars, options));
    QObject::connect(mAction.get(), &QAction::triggered, mAction.get(), [this]() { toggle(); });
    c.addToolbarAction(QStringLiteral("editor.vr"), mAction.get());

    // VR (SPECS/VR_SPEC.md §4.5). Its own row rather than a "Windows" one: it
    // is not a space switch, it is a MODE — the headset comes up and the run
    // happens in it. Listed where it always was, after the filter's Esc row.
    Contributions::Shortcut row;
    row.id = QStringLiteral("vr.toggle");
    row.label = QStringLiteral("Enter / leave VR");
    row.category = QStringLiteral("VR");
    row.keys = QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V);
    row.after = QStringLiteral("properties.filter.clear");
    row.run = [this]() { toggle(); };
    c.addShortcut(row);

    // THE ICON FOLLOWS THE SESSION, not just the button that started it: a
    // session can end from a script (`vr.end()`), from a lost device or from
    // the runtime itself, and a toolbar showing "in VR" over an editor that is
    // not would be a lie.
    //
    // WHAT IT COSTS, stated honestly (lead review F8): in a process that CANNOT
    // do VR (--no-vr, JAHSHAKA_VR=0 — every test run) this is one cached bool
    // and nothing else. In a VR-capable process it is a `VrStatus`
    // read per frame on the UI thread (a ~20-word struct built from the
    // session's own counters, no lock and no runtime call), and the icon is
    // rebuilt only when the answer moves.
    if (host.engine && host.engine->driver()) {
        QObject::connect(host.engine->driver(), &EngineRenderDriver::beforeFrame, mAction.get(),
                         [this]() {
            if (!mCapable) return;
            PlayerService *player = host.services ? host.services->player : nullptr;
            if ((player && player->isVrActive()) != mIconActive
                || (isEditorPreviewActive() && !mIconActive)) {
                const bool wasActive = mIconActive;
                refreshUi();
                // (e) A SESSION THE HEADSET OR RUNTIME DROPPED: the engine has
                // ended it; the editor carries on and the user is told once.
                jahshaka::engine::Engine *e = engineOf(host);
                if (wasActive && !mIconActive && e &&
                    e->vrInfo().failure == jahshaka::engine::VrFailure::ConnectionLost)
                    reportStartFailure(true);
            }
        });
    }
    refreshUi();
    // THE STARTUP HEADSET CHECK, armed only when this run asked for it (Start
    // in VR, or --vr / JAHSHAKA_VR=1). A VR-off run pays nothing.
    if (mCapable && vrLaunch().startCheck) scheduleStartCheck();
}

void VrModule::scheduleStartCheck()
{
    // ONCE THE WINDOW IS UP, not here: the modules contribute inside
    // MainWindow's constructor (the splash may still hold the event loop for
    // the shader build), and a notice shown on a hidden window is a notice
    // nobody saw. Polled at a tenth of a second — it is a one-off.
    QTimer::singleShot(mStartCheckArmed ? 100 : 0, mAction.get(), [this]() {
        mStartCheckArmed = true;
        if (host.shellWidget && !host.shellWidget->isVisible()) { scheduleStartCheck(); return; }
        runStartCheck();
    });
}

void VrModule::runStartCheck()
{
    jahshaka::engine::Engine *e = engineOf(host);
    if (!e || e->vrProbe()) { refreshUi(); return; }
    // THE PREFERENCE'S CHECK IS A NOTICE (the user did not ask for VR this
    // launch); an explicit --vr / JAHSHAKA_VR=1 is answered with the dialog.
    reportStartFailure(vrLaunch().source != QLatin1String("setting"));
    refreshUi();
}

void VrModule::toggle()
{
    PlayerService *player = host.services ? host.services->player : nullptr;
    ProjectService *projects = host.services ? host.services->project : nullptr;
    if (host.shell && host.shell->space() == QLatin1String("editor")) {
        const bool was = isEditorPreviewActive();
        const bool on = toggleEditorPreview();
        // A REFUSED BEGIN IS SAID ON SCREEN (the owner's #51 smoke: "nothing
        // happens when I click it" — the runtime had refused the session six
        // times and the only witness was the log). The verb's own reason is in
        // the host's error slot, where `app.lastError()` reads it.
        if (!was && !on) reportStartFailure(true, scriptHost ? scriptHost->lastError : QString());
        refreshUi();
        return;
    }
    if (!player) return;
    // OFF THE EDITOR PAGE THE BUTTON MEANS THE PLAYER, AND THE PLAYER NEEDS A
    // WORLD (SMOKE-FIX-1's fix round, F7). On a `--vr` boot this icon is live
    // on the DESKTOP page, where there is nothing to play: the toggle used to
    // open the Player page over no project at all and start it. Say so and stop
    // — the service's own refusal is the same predicate, this is the sentence.
    if (!projects || !projects->isSceneOpen()) {
        if (host.shell)
            host.shell->showNotice(QObject::tr("Nothing to play"),
                                   QObject::tr("Open a world first — VR plays the world you have open."));
        refreshUi();
        return;
    }
    const bool wasVr = player->isVrActive();
    if (!player->toggleVr() && !wasVr) {
        qWarning("Jahshaka VR: the toggle did not start - %s", qPrintable(player->lastError()));
        reportStartFailure(true, player->lastError());
    }
    refreshUi();
}

void VrModule::refreshUi()
{
    if (!mAction) return;
    PlayerService *player = host.services ? host.services->player : nullptr;
    const bool available = player && player->vrAvailable();
    const bool previewActive = isEditorPreviewActive();
    const bool active = available && (player->isVrActive() || previewActive);
    // FIXED FOR THE PROCESS (the policy), so it is cached: the per-frame
    // follower tests this before it asks anything else.
    mCapable = available;
    mAction->setEnabled(available);
    mAction->setChecked(active);
    mIconActive = active;
    // THE ICON IS DEAD ONLY WHEN THIS RUN CANNOT DO VR AT ALL (--no-vr,
    // JAHSHAKA_VR=0, headless). A missing headset is the click's answer — the
    // dialog with Try again — never a dead icon (VR-START-1).
    if (available) {
        const bool onEditor = host.shell && host.shell->space() == QLatin1String("editor");
        mAction->setToolTip(active
            ? (previewActive ? QStringLiteral("Leave VR | End the editor preview")
                             : QStringLiteral("Leave VR | Stop the run and take the headset off"))
            : (onEditor ? QStringLiteral("Enter VR | The editor in the headset: point, select and "
                                         "grab with the controllers; the desktop stays the editor")
                        : QStringLiteral("Enter VR | Run the scene in the headset (the Player page, "
                                         "mirrored here)")));
    } else {
        const QString why = player ? player->vrUnavailableReason() : QString();
        mAction->setToolTip(QStringLiteral("Enter VR | Unavailable: %1").arg(why));
    }
    if (host.shell) host.shell->showPlayerVrState(available, active);
}

void VrModule::registerApi(ScriptEngine &engine)
{
    // KEPT, WEAKLY. The ScriptEngine owns the module from here; this pointer is
    // only so shutdown() can end a session through the object that started it
    // (VR-4-FIX finding 7) rather than behind its back — and a QPointer because
    // the scripting engine may well be torn down first.
    scriptHost = &engine.scriptHost();
    api = new VrApi(engine.scriptHost(), host);
    api->setModule(this);
    engine.addModule(api);
}

void VrModule::shutdown()
{
    // A SESSION MUST NOT OUTLIVE THE SHELL. It holds a View on the editor's
    // scene, a second workspace on the viewport's target and a set of XR
    // swapchains; the engine's own destructor ends one too, but by then the
    // shell has already begun taking the scene apart.
    //
    // THROUGH THE VR API, which ends a PREVIEW through EditorVrPreview: that
    // object installed two callbacks on the editor viewport and redirected its
    // fly keys, and ending the session underneath it left all of that in place
    // — harmless at shutdown only for as long as the order never changes,
    // which is not a thing to rely on. The plain path below is the fallback for
    // a session this process's API object cannot reach any more. VrApi::shutdown
    // also detaches the controllers' interaction from the viewport: the API
    // object lives on inside the ScriptEngine until ~MainWindow, after the
    // viewport is gone (VR-TEARDOWN-1).
    if (api && api->shutdown()) return;
    if (!host.engine) return;
    const auto e = host.engine->engine();
    if (!e) return;
    if (e->vrStatus().active) {
        if (host.engine->driver()) host.engine->driver()->setVrSessionActive(false);
        e->setVrMirrorView(nullptr);
        e->endVrSession();
    }
}

bool VrModule::toggleEditorPreview()
{
    if (!api) return false;
    if (api->editorPreviewActive()) { api->end(); return false; }
    return api->begin();
}

bool VrModule::isEditorPreviewActive() const
{
    return api && api->editorPreviewActive();
}
