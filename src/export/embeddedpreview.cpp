/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/embeddedpreview.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLayout>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QWidget>
#include <QWindow>

#ifdef JAH_HAVE_XCB
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#include <cstring>
#endif

namespace {

const char kReadyMarker[] = "[jah-gpu-ready]";
const char kNoGpuMarker[] = "[jah-no-gpu]";

// Our embedded-preview Chrome profiles, and ONLY ours: hidden dirs with this
// prefix inside the export folder, created by this class and nothing else.
const char kProfilePrefix[] = ".preview-profile-embedded";

// Delete leftover embedded-preview profiles from earlier runs (or an earlier
// app that crashed) before launching a new Chrome. A live leftover Chrome on
// such a profile would otherwise capture the new launch through Chrome's
// profile singleton: the new process forwards its URL to the OLD instance
// (which opens a window with the OLD WM_CLASS — unadoptable, outside the app)
// and exits immediately, so the handshake can never succeed. Scoped strictly
// to our own prefix; never touches any other Chrome data.
void purgeStaleProfiles(const QString &exportDirPath)
{
    QDir dir(exportDirPath);
    const QStringList stale = dir.entryList(
        {QString::fromLatin1(kProfilePrefix) + QLatin1Char('*')},
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    for (const QString &name : stale)
        QDir(dir.filePath(name)).removeRecursively();
}

#ifdef JAH_HAVE_XCB

xcb_connection_t *connection()
{
    // The app's OWN xcb connection (EngineHost does the same for Ogre).
    // A second X connection is forbidden — flicker + foreign-content bleed.
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return x11 ? x11->connection() : nullptr;
}

// Depth-limited walk from the root looking for a VIEWABLE window whose
// WM_CLASS contains `token`. Chrome's helper windows are unmapped, so the
// viewable filter selects the real kiosk window. Works with or without a
// reparenting WM (depth covers WM frame wrappers).
xcb_window_t findByClass(xcb_connection_t *c, xcb_window_t node,
                         const QByteArray &token, int depth = 0)
{
    if (depth > 4) return XCB_WINDOW_NONE;
    xcb_query_tree_reply_t *tree =
        xcb_query_tree_reply(c, xcb_query_tree(c, node), nullptr);
    if (!tree) return XCB_WINDOW_NONE;
    xcb_window_t *children = xcb_query_tree_children(tree);
    const int n = xcb_query_tree_children_length(tree);
    xcb_window_t found = XCB_WINDOW_NONE;
    for (int i = 0; i < n && !found; ++i) {
        const xcb_window_t w = children[i];
        xcb_get_property_reply_t *prop = xcb_get_property_reply(
            c, xcb_get_property(c, 0, w, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 0, 256),
            nullptr);
        if (prop) {
            const QByteArray blob(
                static_cast<const char *>(xcb_get_property_value(prop)),
                xcb_get_property_value_length(prop));
            if (blob.contains(token)) {
                xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(
                    c, xcb_get_window_attributes(c, w), nullptr);
                if (attr) {
                    if (attr->map_state == XCB_MAP_STATE_VIEWABLE) found = w;
                    free(attr);
                }
            }
            free(prop);
        }
        if (!found) found = findByClass(c, w, token, depth + 1);
    }
    free(tree);
    return found;
}

bool windowAlive(xcb_connection_t *c, xcb_window_t w)
{
    xcb_generic_error_t *err = nullptr;
    xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(
        c, xcb_get_window_attributes(c, w), &err);
    const bool alive = attr && !err;
    free(attr);
    free(err);
    return alive;
}

QByteArray windowTitle(xcb_connection_t *c, xcb_window_t w)
{
    // _NET_WM_NAME (UTF8_STRING) first — Chrome always sets it; WM_NAME fallback.
    static xcb_atom_t netWmName = XCB_ATOM_NONE;
    static xcb_atom_t utf8String = XCB_ATOM_NONE;
    if (netWmName == XCB_ATOM_NONE) {
        auto intern = [c](const char *name) {
            xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(
                c, xcb_intern_atom(c, 0, quint16(strlen(name)), name), nullptr);
            const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
            free(r);
            return a;
        };
        netWmName = intern("_NET_WM_NAME");
        utf8String = intern("UTF8_STRING");
    }
    QByteArray title;
    xcb_get_property_reply_t *prop = xcb_get_property_reply(
        c, xcb_get_property(c, 0, w, netWmName, utf8String, 0, 1024), nullptr);
    if (prop && xcb_get_property_value_length(prop) > 0)
        title = QByteArray(static_cast<const char *>(xcb_get_property_value(prop)),
                           xcb_get_property_value_length(prop));
    free(prop);
    if (!title.isEmpty()) return title;
    prop = xcb_get_property_reply(
        c, xcb_get_property(c, 0, w, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 1024), nullptr);
    if (prop && xcb_get_property_value_length(prop) > 0)
        title = QByteArray(static_cast<const char *>(xcb_get_property_value(prop)),
                           xcb_get_property_value_length(prop));
    free(prop);
    return title;
}

#endif // JAH_HAVE_XCB

} // namespace

EmbeddedWebPreview::EmbeddedWebPreview(QObject *parent) : QObject(parent) {}

EmbeddedWebPreview::~EmbeddedWebPreview()
{
    stop();
}

bool EmbeddedWebPreview::platformSupported()
{
#ifdef JAH_HAVE_XCB
    return QGuiApplication::platformName() == QLatin1String("xcb") && connection() != nullptr;
#else
    return false;
#endif
}

bool EmbeddedWebPreview::start(const QString &browserPath, const QString &indexHtml,
                               QWidget *hostSlot)
{
#ifndef JAH_HAVE_XCB
    Q_UNUSED(browserPath); Q_UNUSED(indexHtml); Q_UNUSED(hostSlot);
    return false;
#else
    // Reusable after stop() or a finished run — refuse only while a run is
    // actually in flight (re-entry goes through stop() first, by design).
    if (phase != Phase::Idle && phase != Phase::Done) return false;
    if (!platformSupported()) return false;
    const QFileInfo info(indexHtml);
    if (browserPath.isEmpty() || !info.exists() || !hostSlot || !hostSlot->layout())
        return false;
    stop(); // clears any Done-state leftovers (proc, container, profile dir)

    slot = hostSlot;
    // Unique per attempt so a stale window from a previous run can't match —
    // and the SAME uniqueness for the Chrome profile below, so a previous
    // run's Chrome (live or half-dead) can never singleton-capture this one.
    const QString uniq = QUuid::createUuid().toString(QUuid::Id128).left(12);
    classToken = "JahWebPreview-" + uniq.toUtf8();
    profileDir = info.absolutePath() + QLatin1Char('/') +
                 QString::fromLatin1(kProfilePrefix) + QLatin1Char('-') + uniq;
    purgeStaleProfiles(info.absolutePath());

    // ?jahembed=1 makes OUR viewer append readiness markers to its title;
    // exported pages opened normally never carry the marker.
    QUrl url = QUrl::fromLocalFile(info.absoluteFilePath());
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("jahembed"), QStringLiteral("1"));
    url.setQuery(query);

    proc = new QProcess(this);
    proc->setProgram(browserPath);
    proc->setArguments({
        QStringLiteral("--app=%1").arg(url.toString()),
        QStringLiteral("--class=%1").arg(QString::fromUtf8(classToken)),
        // Wayland Chrome has no X window to adopt; and X11 Chrome only gets a
        // real WebGPU adapter with the Vulkan feature (spike-verified).
        QStringLiteral("--ozone-platform=x11"),
        QStringLiteral("--enable-features=Vulkan"),
        // Distinct AND unique-per-run profile: never contend with a running
        // companion preview, and never with a previous embedded run's Chrome
        // (same-profile launches get singleton-forwarded to the old instance).
        QStringLiteral("--user-data-dir=%1").arg(profileDir),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        QStringLiteral("--window-size=1280,800"),
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                if (phase == Phase::Embedded)
                    finish(true, QString());
                else if (phase == Phase::Finding || phase == Phase::WaitingReady)
                    finish(false, QStringLiteral("browser exited before it could be embedded"));
            });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (phase == Phase::Finding || phase == Phase::WaitingReady)
            finish(false, QStringLiteral("browser failed to launch"));
    });
    proc->start();

    phase = Phase::Finding;
    phaseStartMs = QDateTime::currentMSecsSinceEpoch();
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &EmbeddedWebPreview::poll);
    timer->start(250);
    return true;
#endif
}

void EmbeddedWebPreview::poll()
{
#ifdef JAH_HAVE_XCB
    xcb_connection_t *c = connection();
    if (!c) { finish(phase == Phase::Embedded, QStringLiteral("no X connection")); return; }
    const qint64 inPhase = QDateTime::currentMSecsSinceEpoch() - phaseStartMs;

    switch (phase) {
    case Phase::Finding: {
        const xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;
        const xcb_window_t w = findByClass(c, root, classToken);
        if (w != XCB_WINDOW_NONE) {
            windowId = w;
            phase = Phase::WaitingReady;
            phaseStartMs = QDateTime::currentMSecsSinceEpoch();
        } else if (inPhase > (refinding ? refindGraceMs : findTimeoutMs)) {
            finish(everEmbedded, QStringLiteral("browser window not found"));
        }
        break;
    }
    case Phase::WaitingReady: {
        if (!windowAlive(c, xcb_window_t(windowId))) {
            // window replaced before ready (rare) — go find the new one
            windowId = 0;
            phase = Phase::Finding;
            phaseStartMs = QDateTime::currentMSecsSinceEpoch();
            break;
        }
        const QByteArray title = windowTitle(c, xcb_window_t(windowId));
        if (title.contains(kNoGpuMarker)) {
            finish(everEmbedded, QStringLiteral("viewer reported no WebGPU in embeddable browser"));
        } else if (title.contains(kReadyMarker)) {
            // ADOPT ONLY NOW: reparenting during Chrome's GPU-process init
            // permanently kills its WebGPU adapter (spike-verified).
            adopt(windowId);
        } else if (inPhase > readyTimeoutMs) {
            finish(everEmbedded, QStringLiteral("viewer never signalled gpu-ready"));
        }
        break;
    }
    case Phase::Embedded: {
        if (windowAlive(c, xcb_window_t(windowId))) break;
        // Chrome recreated (or lost) its window. Drop the dead container and
        // give the watchdog a grace period to re-adopt (spike-verified path).
        // detached() lets the host hide the embed area meanwhile — a visible
        // slot with no container is a dead black panel.
        delete container;
        container = nullptr;
        foreignWindow = nullptr;
        windowId = 0;
        if (proc && proc->state() == QProcess::Running) {
            phase = Phase::Finding;
            phaseStartMs = QDateTime::currentMSecsSinceEpoch();
            refinding = true; // grace timeout, without mutating findTimeoutMs
            emit detached();
        } else {
            finish(true, QString());
        }
        break;
    }
    case Phase::Idle:
    case Phase::Done:
        break;
    }
#endif
}

void EmbeddedWebPreview::adopt(quint32 wid)
{
#ifdef JAH_HAVE_XCB
    if (!slot || !slot->layout()) {
        finish(false, QStringLiteral("host slot vanished"));
        return;
    }
    foreignWindow = QWindow::fromWinId(WId(wid));
    if (!foreignWindow) {
        finish(false, QStringLiteral("could not wrap the browser window"));
        return;
    }
    container = QWidget::createWindowContainer(foreignWindow, slot);
    container->setFocusPolicy(Qt::StrongFocus);
    slot->layout()->addWidget(container);
    phase = Phase::Embedded;
    phaseStartMs = QDateTime::currentMSecsSinceEpoch();
    everEmbedded = true;
    refinding = false;
    emit embedded();
#else
    Q_UNUSED(wid);
#endif
}

void EmbeddedWebPreview::finish(bool wasEmbedded, const QString &reason)
{
    if (phase == Phase::Done || phase == Phase::Idle) return;
    phase = Phase::Done;
    if (timer) timer->stop();
    delete container;
    container = nullptr;
    foreignWindow = nullptr;
    // A session that WAS embedded ends as closed(), never failed() — failed()
    // makes the caller fall back to a companion window, which must only
    // happen for a preview the user never saw embedded.
    if (wasEmbedded || everEmbedded) emit closed();
    else emit failed(reason);
}

void EmbeddedWebPreview::stop()
{
    // Full teardown back to Idle: the same object (or a successor using the
    // same export dir) must be able to start a FRESH run with nothing of this
    // one left — no container over a dead window, no owned Chrome, no profile
    // dir a new Chrome could singleton-collide with. Order: container first
    // (the prior Close-preview fix proved destroying the container before the
    // foreign window dies is the safe direction), then the process, then the
    // profile once the process is down.
    phase = Phase::Idle;
    if (timer) { timer->stop(); timer->deleteLater(); timer = nullptr; }
    delete container;
    container = nullptr;
    foreignWindow = nullptr;
    windowId = 0;
    everEmbedded = false;
    refinding = false;
    classToken.clear();
    if (proc) {
        proc->disconnect(this); // its finished-lambda must not re-enter finish()
        if (proc->state() != QProcess::NotRunning) {
            proc->terminate();
            if (!proc->waitForFinished(1500)) {
                proc->kill();
                proc->waitForFinished(500);
            }
        }
        proc->deleteLater();
        proc = nullptr;
    }
    if (!profileDir.isEmpty()) {
        QDir(profileDir).removeRecursively(); // ours alone, by construction
        profileDir.clear();
    }
}
