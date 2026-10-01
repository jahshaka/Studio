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

#include "bridge/enginehost.h"
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
    // do VR — every ordinary launch, since capability is fixed at boot and only
    // `--vr` asks for it — this is one cached bool and nothing else, which is
    // the case that must not pay. In a VR-capable process it is a `VrStatus`
    // read per frame on the UI thread (a ~20-word struct built from the
    // session's own counters, no lock and no runtime call), and the icon is
    // rebuilt only when the answer moves.
    if (host.engine && host.engine->driver()) {
        QObject::connect(host.engine->driver(), &EngineRenderDriver::beforeFrame, mAction.get(),
                         [this]() {
            if (!mCapable) return;
            PlayerService *player = host.services ? host.services->player : nullptr;
            if ((player && player->isVrActive()) != mIconActive
                || (isEditorPreviewActive() && !mIconActive))
                refreshUi();
        });
    }
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
        if (!was && !on) showRefusal(scriptHost ? scriptHost->lastError : QString());
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
        showRefusal(player->lastError());
    }
    refreshUi();
}

void VrModule::showRefusal(const QString &reason)
{
    // THE SENTENCE A PERSON CAN ACT ON FIRST, the runtime's own words second.
    // One case deserves its own sentence because nothing in the reason says
    // what to DO: the OpenXR runtime created this process's Vulkan device at
    // boot, so a runtime connection that died afterwards — WiVRn starts a fresh
    // streaming process every time the headset reconnects, and the one this
    // app connected to is gone — cannot be re-made in place. Every
    // xrCreateSession then fails XR_ERROR_RUNTIME_FAILURE for the life of the
    // process (measured, the owner's #51 smoke).
    const bool connectionDied = reason.contains(QLatin1String("XR_ERROR_RUNTIME_FAILURE"))
                             || reason.contains(QLatin1String("XR_ERROR_INSTANCE_LOST"))
                             || reason.contains(QLatin1String("XR_ERROR_RUNTIME_UNAVAILABLE"));
    QString text = connectionDied
        ? QObject::tr("The headset's connection changed after Jahshaka started. Put the headset on, "
                      "check it is connected, then restart Jahshaka.")
        : QObject::tr("The headset did not start.");
    if (!reason.isEmpty()) text += QStringLiteral("\n") + reason;
    if (host.shell) host.shell->showNotice(QObject::tr("VR did not start"), text);
}

void VrModule::refreshUi()
{
    if (!mAction) return;
    PlayerService *player = host.services ? host.services->player : nullptr;
    const bool available = player && player->vrAvailable();
    const bool previewActive = isEditorPreviewActive();
    const bool active = available && (player->isVrActive() || previewActive);
    // FIXED AT BOOT, so it is asked once and cached: the per-frame follower
    // tests this before it asks anything else.
    mCapable = available;
    mAction->setEnabled(available);
    mAction->setChecked(active);
    mIconActive = active;
    // THE TOOLTIP CARRIES THE RUNTIME'S OWN REASON when the icon is dead, plus
    // the sentence a user can act on: VR capability is decided once, at boot,
    // because the OpenXR route has the RUNTIME create the Vulkan device the
    // whole engine runs on (VR_SPEC §7 risk 11). Plugging a headset in later
    // needs a restart, and nothing in the editor can change that at runtime.
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
        QString why = player ? player->vrUnavailableReason() : QString();
        if (!cliVr())
            why = QStringLiteral("VR capability is fixed at boot — restart with --vr");
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
    // a session this process's API object cannot reach any more.
    if (api && api->endForShutdown()) return;
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
