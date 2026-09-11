/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.toast — WHERE A TOAST SITS (owner smoke S8, 2026-09-11: "the
// Add-to-Project toast is not centred; it should sit bottom-centre of the app
// window") and audit F-D4 (the two shell call sites moved the toast with
// GLOBAL coordinates, which is right only while the window sits at the
// screen's origin — i.e. on the test rig and nowhere else).
//
// So the WINDOW IN THIS SUITE IS NOT AT THE ORIGIN. That is the whole point:
// the old arrangement passes every assertion here with the window at (0,0) and
// fails all of them at (320, 180).
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdio>

#include "ui/dialogs/toast.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
} while (0)

static void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QWidget window;
    window.resize(1024, 768);
    window.move(320, 180);           // NOT the screen's origin (F-D4)
    auto *viewport = new QWidget(&window);
    viewport->setGeometry(220, 40, 700, 500);
    window.show();
    pump(30);

    // ---- the default: bottom-centre of the app window ---------------------
    {
        Toast toast(&window);
        toast.showToast(QStringLiteral("Asset Added To Project"),
                        QStringLiteral("Cube has been added successfully to the open project."));
        pump(30);
        const QRect r = toast.geometryInWindow();
        std::printf("    toast in window: %d,%d %dx%d (window %dx%d)\n",
                    r.x(), r.y(), r.width(), r.height(), window.width(), window.height());
        CHECK(toast.isVisible(), "the toast is shown");
        CHECK(window.rect().contains(r),
              "THE TOAST IS INSIDE THE WINDOW (F-D4: a global point moved as a local one is not)");
        CHECK(std::abs(r.center().x() - window.rect().center().x()) <= 2,
              "it is centred horizontally on the window");
        CHECK(r.bottom() < window.rect().bottom() && r.bottom() > window.height() - 80,
              "it is anchored to the BOTTOM edge, with a margin");
        CHECK(r.top() > window.height() / 2, "it is in the bottom half, not floating in the middle");

        // A window that moves takes its toast with it.
        const QRect beforeMove = toast.geometryInWindow();
        window.move(600, 400);
        pump(30);
        CHECK(toast.geometryInWindow() == beforeMove,
              "moving the window keeps the toast on its anchor (same place IN the window)");

        // And a resize re-centres it.
        window.resize(1280, 900);
        pump(30);
        const QRect afterResize = toast.geometryInWindow();
        CHECK(std::abs(afterResize.center().x() - window.rect().center().x()) <= 2,
              "a resized window re-centres the toast");
        CHECK(afterResize.bottom() > window.height() - 80,
              "and it stays on the bottom edge");
    }

    // ---- the viewport readout: top-centre of a named widget ----------------
    {
        Toast toast(&window);
        toast.setAnchor(Toast::Anchor::WidgetTop, viewport);
        toast.showToast(QStringLiteral("Translate snap"), QStringLiteral("0.5 m"));
        pump(30);
        const QRect r = toast.geometryInWindow();
        const QRect vp(viewport->mapTo(&window, QPoint(0, 0)), viewport->size());
        std::printf("    readout in window: %d,%d %dx%d (viewport %d,%d %dx%d)\n",
                    r.x(), r.y(), r.width(), r.height(), vp.x(), vp.y(), vp.width(), vp.height());
        CHECK(std::abs(r.center().x() - vp.center().x()) <= 2,
              "the viewport readout is centred on the VIEWPORT, not the window");
        CHECK(r.top() >= vp.top() && r.top() < vp.top() + 64,
              "and sits just inside its top edge");
    }

    // ---- a REUSED toast: the second message gets the full hold (F-D6) -----
    {
        Toast toast(&window);
        toast.showToast(QStringLiteral("First"), QStringLiteral("gone in a moment"), 60);
        toast.showToast(QStringLiteral("Second"), QStringLiteral("the one being read"), 1500);
        pump(300);
        CHECK(toast.isVisible(),
              "the first message's timer did NOT hide the second (one restarted hold timer)");
        toast.hide();
    }

    // ---- the centre anchor (the blocking "3D view unavailable" message) ----
    {
        Toast toast(&window);
        toast.setAnchor(Toast::Anchor::WindowCentre);
        toast.showToast(QStringLiteral("3D view unavailable"),
                        QStringLiteral("The 3D view could not be created: no device"));
        pump(30);
        const QRect r = toast.geometryInWindow();
        CHECK(window.rect().contains(r), "the centred toast is inside the window too");
        CHECK(std::abs(r.center().x() - window.rect().center().x()) <= 2
                  && std::abs(r.center().y() - window.rect().center().y()) <= 2,
              "and it is in the middle of it");
    }

    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
