/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SELFTESTRUNNER_H
#define SELFTESTRUNNER_H

// --engine-selftest <out.png> (audit §4.2: app/cli/).
//
// The self-verification path for the engine viewport (VIEWPORT_MIGRATION_PLAN
// step 6): MainWindow built normally, its default scene created and set on the
// viewport exactly as newScene() does at runtime, ~40 frames pumped (with two
// resizes — the engine must survive swapchain rebuilds), one offscreen
// screenshot saved. Exit 0 iff the image exists and its centre pixel is not
// the clear colour — i.e. the ground plane rendered.

class MainWindow;
class QApplication;
class QString;

int runEngineSelftest(MainWindow &window, QApplication &app, const QString &outPng);

#endif // SELFTESTRUNNER_H
