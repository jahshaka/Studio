/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/capture/capturemodule.h"

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QWidget>

#include "data/project.h"
#include "data/settingsmanager.h"
#include "data/settingsstore.h"
#include "modules/capture/captureapi.h"
#include "modules/capture/videorecorder.h"
#include "scripting/scriptengine.h"
#include "thirdparty/qtawesome/QtAwesome.h"
#include "ui/controls/fonticons.h"
#include "viewport/ieditorviewport.h"

namespace {
const char *kHelpersKey = "capture/helpers";
const char *kModeKey = "capture/mode";

QString clock(double seconds)
{
    const int s = int(seconds);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
}   // namespace

CaptureModule::CaptureModule() = default;
CaptureModule::~CaptureModule() = default;

void CaptureModule::initialize(StudioContext &ctx)
{
    host = ctx;
    // ONLY WHERE THERE IS A PICTURE: a headless host has no engine viewport, and
    // every verb then refuses with that reason.
    if (!host.viewport) return;
    Project *project = host.project;
    mRecorder = std::make_unique<VideoRecorder>(host.viewport, [project]() {
        return project ? project->getProjectName() : QString();
    });
    QObject::connect(mRecorder.get(), &VideoRecorder::stateChanged, mRecorder.get(), [this]() { refreshUi(); });
    // THE ELAPSED TIME follows the recorded frames (a scripted step and a driver
    // frame alike), rebuilt once a second of video.
    QObject::connect(mRecorder.get(), &VideoRecorder::elapsedChanged, mRecorder.get(), [this]() { refreshUi(); });
    QObject::connect(mRecorder.get(), &VideoRecorder::finished, mRecorder.get(),
                     [this](bool ok, const QString &path, const QString &error) { onFinished(ok, path, error); });
}

bool CaptureModule::helpersSwitch() const
{
    SettingsManager *sm = host.settings ? host.settings : SettingsManager::getDefaultManager();
    return sm && sm->settings ? sm->settings->value(kHelpersKey, false).toBool() : false;
}

void CaptureModule::setHelpersSwitch(bool on)
{
    SettingsManager *sm = host.settings ? host.settings : SettingsManager::getDefaultManager();
    if (sm && sm->settings) sm->settings->setValue(kHelpersKey, on);
    if (mHelpersRow && mHelpersRow->isChecked() != on) mHelpersRow->setChecked(on);
}

VideoRecorder::Mode CaptureModule::modeSwitch() const
{
    SettingsManager *sm = host.settings ? host.settings : SettingsManager::getDefaultManager();
    VideoRecorder::Mode m = VideoRecorder::Mode::Realtime;
    if (sm && sm->settings) VideoRecorder::parseMode(sm->settings->value(kModeKey, QStringLiteral("realtime")).toString(), &m);
    return m;
}

void CaptureModule::setModeSwitch(VideoRecorder::Mode mode)
{
    SettingsManager *sm = host.settings ? host.settings : SettingsManager::getDefaultManager();
    if (sm && sm->settings) sm->settings->setValue(kModeKey, VideoRecorder::modeName(mode));
    QAction *row = mode == VideoRecorder::Mode::Offline ? mOfflineRow : mRealtimeRow;
    if (row && !row->isChecked()) row->setChecked(true);
}

void CaptureModule::contribute(Contributions &c)
{
    mAction.reset(new QAction);
    mAction->setObjectName(QStringLiteral("actionRecord"));
    QObject::connect(mAction.get(), &QAction::triggered, mAction.get(), [this]() { press(); });

    // THE POPUP: the switch, then (after a recording) the file and its folder.
    mMenu.reset(new QMenu);
    mMenu->setObjectName(QStringLiteral("recordMenu"));
    // THE MODE (VIDEO-REC-2): two exclusive rows, real-time by default.
    auto *modes = new QActionGroup(mMenu.get());
    modes->setExclusive(true);
    mRealtimeRow = mMenu->addAction(QObject::tr("Real time (the scene's own clock)"));
    // Each drawn frame is 1/60 s of the world while recording offline: a slow
    // editor runs behind the wall clock, and a FAST panel (144 Hz) runs the
    // editor's world faster than real time — said on the row itself.
    mOfflineRow = mMenu->addAction(QObject::tr("Offline: perfect 60 fps (1/60 s of the world per drawn frame: "
                                               "slower than real time, faster on a fast display)"));
    for (QAction *row : { mRealtimeRow, mOfflineRow }) {
        row->setCheckable(true);
        modes->addAction(row);
    }
    (modeSwitch() == VideoRecorder::Mode::Offline ? mOfflineRow : mRealtimeRow)->setChecked(true);
    QObject::connect(mRealtimeRow, &QAction::toggled, mMenu.get(), [this](bool on) {
        if (on) setModeSwitch(VideoRecorder::Mode::Realtime);
    });
    QObject::connect(mOfflineRow, &QAction::toggled, mMenu.get(), [this](bool on) {
        if (on) setModeSwitch(VideoRecorder::Mode::Offline);
    });
    mMenu->addSeparator();
    mHelpersRow = mMenu->addAction(QObject::tr("Include editor helpers (grid, gizmo, selection)"));
    mHelpersRow->setCheckable(true);
    mHelpersRow->setChecked(helpersSwitch());
    QObject::connect(mHelpersRow, &QAction::toggled, mMenu.get(), [this](bool on) { setHelpersSwitch(on); });
    mMenu->addSeparator();
    mFileRow = mMenu->addAction(QString());
    mFileRow->setEnabled(false);
    mFileRow->setVisible(false);
    mOpenFolderRow = mMenu->addAction(QObject::tr("Open folder"));
    mOpenFolderRow->setVisible(false);
    QObject::connect(mOpenFolderRow, &QAction::triggered, mMenu.get(), [this]() {
        const QString path = mRecorder ? mRecorder->lastPath() : QString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    mAction->setMenu(mMenu.get());
    c.addToolbarAction(QStringLiteral("editor.capture"), mAction.get());

    // Esc is a viewport MODE key while recording (the pilot's eject is the
    // other one) — listed here so the Preferences page can show it.
    Contributions::FixedRow esc;
    esc.id = QStringLiteral("capture.stop.esc");
    esc.label = QObject::tr("Stop recording (while recording)");
    esc.category = QObject::tr("Viewport");
    esc.text = QStringLiteral("Esc");
    c.addFixedRow(esc);

    refreshUi();
}

void CaptureModule::registerApi(ScriptEngine &engine)
{
    mApi = new CaptureApi(engine.scriptHost(), this);
    engine.addModule(mApi);
}

void CaptureModule::onProjectChanged(Project *project)
{
    // A close ends a recording through the viewport's own teardown hook
    // (RecordingHooks::ends, before the engine scene goes); nothing else here.
    Q_UNUSED(project);
    refreshUi();
}

void CaptureModule::styleButton()
{
    if (mButton || !mAction) return;
    for (QObject *o : mAction->associatedObjects()) {
        if (auto *b = qobject_cast<QToolButton *>(o)) { mButton = b; break; }
    }
    if (!mButton) return;
    // A CLICK RECORDS; the popup is press-and-hold or a right-click. (The split
    // arrow of MenuButtonPopup makes the button 2 px taller than the bar and
    // wider than the window's floor allows.)
    mButton->setPopupMode(QToolButton::DelayedPopup);
    mButton->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(mButton, &QWidget::customContextMenuRequested, mButton, [this](const QPoint &at) {
        if (mMenu && mButton) mMenu->popup(mButton->mapToGlobal(at));
    });
}

void CaptureModule::refreshUi()
{
    if (!mAction) return;
    styleButton();
    const bool recording = mRecorder && mRecorder->recording();
    const bool finishing = mRecorder && mRecorder->state() == VideoRecorder::State::Finishing;
    const bool offline = recording && mRecorder->mode() == VideoRecorder::Mode::Offline;
    // An offline recording's wall time moves on its own: ticked twice a second.
    if (offline && !mWallTicker) {
        mWallTicker = std::make_unique<QTimer>();
        mWallTicker->setInterval(500);
        QObject::connect(mWallTicker.get(), &QTimer::timeout, mWallTicker.get(), [this]() { refreshUi(); });
        mWallTicker->start();
    } else if (!offline && mWallTicker) {
        // Possibly from inside the ticker's own timeout: never deleted in its signal.
        mWallTicker.release()->deleteLater();
    }
    QString text;
    // OFFLINE: the clip time AGAINST the wall time (the brief's "the user sees
    // offline is slower than real time").
    if (offline) text = QStringLiteral("%1 / %2").arg(clock(mRecorder->elapsedSeconds()), clock(mRecorder->wallSeconds()));
    else if (recording) text = clock(mRecorder->elapsedSeconds());
    else if (finishing) text = QObject::tr("Saving…");
    else text = QObject::tr("Record");
    if (text == mLastShownText && mAction->property("jahRed").toBool() == recording) return;
    mLastShownText = text;
    QVariantMap options;
    // THE INDICATOR (§10.6): a red dot while recording, the camera otherwise.
    const QColor red(231, 76, 60), white(255, 255, 255);
    options.insert("color", recording ? red : white);
    options.insert("color-active", recording ? red : white);
    mAction->setIcon(fonticons::shared().icon(recording ? fa::circle : fa::videocamera, options));
    mAction->setText(text);
    mAction->setProperty("jahRed", recording);
    // ICON ONLY while idle, like the photo button beside it (the bar's width is
    // the window's minimum); the elapsed time beside the red dot while recording.
    if (mButton) mButton->setToolButtonStyle(recording ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    mAction->setEnabled(bool(mRecorder) && !finishing);
    const QString last = mRecorder ? mRecorder->lastPath() : QString();
    QString tip;
    if (!mRecorder) tip = QObject::tr("Recording needs the engine viewport");
    else if (offline)
        tip = QObject::tr("Recording OFFLINE (perfect 60 fps) %1 — the clip time / the wall time; "
                          "click, or press Esc in the viewport, to stop")
                  .arg(QDir::toNativeSeparators(mRecorder->status().value("path").toString()));
    else if (recording)
        tip = QObject::tr("Recording %1 — click, or press Esc in the viewport, to stop")
                  .arg(QDir::toNativeSeparators(mRecorder->status().value("path").toString()));
    else {
        tip = QObject::tr("Record a 1920x1080, 60 fps video of this camera (hold or right-click: options)");
        if (!last.isEmpty())
            tip += QObject::tr("\nSaved: %1 — hold or right-click to open its folder").arg(QDir::toNativeSeparators(last));
    }
    mAction->setToolTip(tip);
    if (mFileRow) {
        mFileRow->setVisible(!last.isEmpty());
        mFileRow->setText(QObject::tr("Saved: %1").arg(QFileInfo(last).fileName()));
    }
    if (mOpenFolderRow) mOpenFolderRow->setVisible(!last.isEmpty());
}

bool CaptureModule::press()
{
    if (!mRecorder) return false;
    if (mRecorder->recording()) {
        return mRecorder->stop();
    }
    if (mRecorder->busy()) return false;
    VideoRecorder::Options o;
    o.helpers = helpersSwitch();
    o.mode = modeSwitch();
    QString why;
    if (!mRecorder->start(o, &why)) {
        reportFailure(why);
        return false;
    }
    mLastFailure.clear();
    refreshUi();
    return true;
}

void CaptureModule::onFinished(bool ok, const QString &path, const QString &error)
{
    Q_UNUSED(path);
    refreshUi();
    if (!ok) {
        // ONE truthful message for a recording that failed while it ran.
        reportFailure(error);
        return;
    }
    // THE FILE IS ONE CLICK AWAY (§10.6: "Open folder" in the button's popup):
    // the popup's rows name it, and the button's tooltip says where. The popup
    // is NOT opened by itself — a menu that appears when an encoder finishes
    // would grab the mouse from whatever the user is doing by then.
}

void CaptureModule::reportFailure(const QString &message)
{
    mLastFailure = message;
    QWidget *parent = host.shellWidget;
    if (!mFailureDialog) {
        mFailureDialog = new QMessageBox(QMessageBox::Warning, QObject::tr("Recording stopped"), QString(),
                                         QMessageBox::Ok, parent);
        mFailureDialog->setObjectName(QStringLiteral("captureFailureDialog"));
        mFailureDialog->setAttribute(Qt::WA_DeleteOnClose);
        mFailureDialog->setModal(false);
    }
    mFailureDialog->setText(QObject::tr("The video recording could not continue."));
    mFailureDialog->setInformativeText(message);
    mFailureDialog->show();
    mFailureDialog->raise();
}

bool CaptureModule::dismissFailure()
{
    if (!mFailureDialog || !mFailureDialog->isVisible()) return false;
    mFailureDialog->close();
    return true;
}

QVariantMap CaptureModule::button() const
{
    QVariantMap m;
    if (!mAction) return m;
    m["text"] = mAction->text();
    m["toolTip"] = mAction->toolTip();
    m["enabled"] = mAction->isEnabled();
    m["red"] = mAction->property("jahRed").toBool();
    m["recording"] = mRecorder && mRecorder->recording();
    m["helpers"] = helpersSwitch();
    m["mode"] = VideoRecorder::modeName(modeSwitch());
    m["placed"] = bool(mButton);
    m["visible"] = mButton && mButton->isVisible();
    if (mButton) {
        m["width"] = mButton->width();
        m["height"] = mButton->height();
        if (QWidget *bar = mButton->parentWidget()) m["barHeight"] = bar->height();
    }
    QStringList rows;
    if (mMenu)
        for (QAction *a : mMenu->actions())
            if (!a->isSeparator() && a->isVisible()) rows << a->text();
    m["menu"] = rows;
    m["menuOpen"] = mMenu && mMenu->isVisible();
    m["failure"] = mLastFailure;
    m["dialogOpen"] = mFailureDialog && mFailureDialog->isVisible();
    return m;
}

void CaptureModule::abortBackgroundWork()
{
    // STOP, DO NOT JOIN: a running recording is ended now (its drain is three
    // frames at most); the fast-start worker rides the shell's pool wait.
    if (mRecorder && mRecorder->recording()) mRecorder->stop();
}

void CaptureModule::shutdown()
{
    if (mRecorder) {
        if (mRecorder->recording()) mRecorder->stop();
        // The encoder's own stop is asynchronous: give it the shell's budget.
        mRecorder->waitFinished(3000);
        // A SAVE STILL RUNNING is not thrown away: the encoder's own complete
        // file is published at the final path (VideoRecorder::abandonSave).
        mRecorder->abandonSave();
    }
    if (mFailureDialog) mFailureDialog->close();
    if (mMenu) mMenu->close();
    mWallTicker.reset();
    mAction.reset();
    mMenu.reset();
    mRecorder.reset();
}
