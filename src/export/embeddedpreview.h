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

    // Tear down: destroy the container and terminate the browser process.
    // Safe to call in any state, including after failed()/closed().
    void stop();

    bool isEmbedded() const { return phase == Phase::Embedded; }
    QProcess *process() const { return proc; }

    // Timeouts (ms), overridable for tests.
    int findTimeoutMs = 10000;    // window with our WM_CLASS token must appear
    int readyTimeoutMs = 25000;   // viewer must reach its first rendered frame
    int refindGraceMs = 5000;     // window recreation grace before giving up

signals:
    void embedded();                    // container is live inside hostSlot
    void failed(const QString &reason); // never embedded — fall back silently
    void closed();                      // was embedded, browser went away

private:
    enum class Phase { Idle, Finding, WaitingReady, Embedded, Done };

    void poll();
    void adopt(quint32 wid);
    void finish(bool wasEmbedded, const QString &reason);

    Phase phase = Phase::Idle;
    QByteArray classToken;
    quint32 windowId = 0;
    qint64 phaseStartMs = 0;
    QPointer<QWidget> slot;
    QPointer<QWidget> container;
    QWindow *foreignWindow = nullptr;
    QProcess *proc = nullptr;
    QTimer *timer = nullptr;
};

#endif // EMBEDDEDPREVIEW_H
