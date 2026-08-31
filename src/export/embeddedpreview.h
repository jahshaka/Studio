/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EMBEDDEDPREVIEW_H
#define EMBEDDEDPREVIEW_H

// EmbeddedWebPreview — hosts the Publish page's Chrome `--app` WebGPU preview
// INSIDE the Jahshaka window (Linux/X11 only) by adopting Chrome's top-level
// X window with QWindow::fromWinId + QWidget::createWindowContainer.
//
// Facts this design rests on (verified in the 2026-08-31 embed spike; details
// in the worktree report):
//  - Chrome under a Wayland session picks its Wayland backend — there is no X
//    window to adopt unless `--ozone-platform=x11` is forced.
//  - X11 Chrome gets NO WebGPU adapter by default on this driver;
//    `--enable-features=Vulkan` yields the real GPU adapter
//    (`--enable-unsafe-webgpu` only yields SwiftShader plus a scare banner).
//  - Reparenting Chrome's window while its GPU process is still initializing
//    kills WebGPU PERMANENTLY for that Chrome (requestAdapter keeps returning
//    null on retries). Adopting after frames are flowing is stable (30s+ soak,
//    swapchain reconfigure while embedded works). Hence the handshake: the
//    viewer appends "[jah-gpu-ready]" to its title on the first rendered frame
//    (only when launched with ?jahembed=1) and adoption waits for it.
//  - The window is found and watched via Qt's OWN xcb connection
//    (QNativeInterface::QX11Application) — never a second X connection.
//  - A killed/recreated Chrome window is detected by an xcb liveness probe and
//    can be re-adopted in the same process (proven); otherwise closed() fires.
//
// Every failure path emits failed()/closed() with no dialogs — the caller
// degrades to the companion-window flow that exists today.

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>

class QWidget;
class QWindow;
class QTimer;

class EmbeddedWebPreview : public QObject
{
    Q_OBJECT
public:
    explicit EmbeddedWebPreview(QObject *parent = nullptr);
    ~EmbeddedWebPreview() override;

    // True only when adoption can work at all: Linux, xcb platform, live X
    // connection. False => callers must use the companion window directly.
    static bool platformSupported();

    // Launch `browserPath` on `indexHtml` as an embeddable kiosk and begin the
    // find -> wait-for-gpu-ready -> adopt sequence into `hostSlot` (which must
    // have a layout). Returns false without side effects when preconditions
    // fail. Progress is reported ONLY through the signals.
    bool start(const QString &browserPath, const QString &indexHtml, QWidget *hostSlot);

    // Tear down COMPLETELY: destroy the container, terminate and reap the
    // browser process, delete this run's Chrome profile dir, and return to
    // Idle so start() can run again on the same object. Safe to call in any
    // state, including after failed()/closed(). Emits nothing.
    void stop();

    bool isEmbedded() const { return phase == Phase::Embedded; }
    // The owned browser process for the CURRENT run; null once stop() reaped it.
    QProcess *process() const { return proc; }
    // This run's unique --user-data-dir (for tests); empty after stop().
    QString profileDirPath() const { return profileDir; }

    // Timeouts (ms), overridable for tests.
    int findTimeoutMs = 10000;    // window with our WM_CLASS token must appear
    int readyTimeoutMs = 25000;   // viewer must reach its first rendered frame
    int refindGraceMs = 5000;     // window recreation grace before giving up

signals:
    void embedded();                    // container is live inside hostSlot
                                        // (fires again after a re-adoption)
    void failed(const QString &reason); // NEVER embedded — fall back silently
    void closed();                      // WAS embedded at some point, browser
                                        // (or its window) went away for good
    void detached();                    // was embedded, container dropped while
                                        // the watchdog re-finds the window —
                                        // hide the embed area until embedded()
                                        // fires again or closed() ends the run

private:
    enum class Phase { Idle, Finding, WaitingReady, Embedded, Done };

    void poll();
    void adopt(quint32 wid);
    void finish(bool wasEmbedded, const QString &reason);

    Phase phase = Phase::Idle;
    QByteArray classToken;
    QString profileDir;         // unique per run — Chrome singleton isolation
    quint32 windowId = 0;
    qint64 phaseStartMs = 0;
    bool everEmbedded = false;  // a lost window after success is closed(), not failed()
    bool refinding = false;     // Finding is a watchdog re-find (grace timeout)
    QPointer<QWidget> slot;
    QPointer<QWidget> container;
    QWindow *foreignWindow = nullptr;
    QProcess *proc = nullptr;
    QTimer *timer = nullptr;
};

#endif // EMBEDDEDPREVIEW_H
