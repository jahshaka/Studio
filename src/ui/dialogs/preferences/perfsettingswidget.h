/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PERFSETTINGSWIDGET_H
#define PERFSETTINGSWIDGET_H

// The Performance page of the Preferences dialog — the render monitor's two
// settings and nothing else (RENDER_LOOP_MONITOR_SPEC.md §4.8; CLEANUP-1 item
// 11).
//
// WHY IT EXISTS. The capture ROOT had no door at all: the default was a
// compiled-in `~/Developer/spikes/perf` — one developer's workspace path, in
// shipping code — and the only ways to change it were an environment variable
// and a preference key nothing wrote. The default is now this run's data
// directory (AppPaths, so it follows --data-root like everything else), and
// this page is where somebody who wants their captures somewhere specific says
// so. The developer path is one Browse away.
//
// It also carries the capture LENGTH (Ctrl+F4 records this many seconds) and
// the housekeeping the capture start performs: bundles are big, and a folder
// nothing ever sweeps is a disk leak.
//
// EVERYTHING HERE IS A PREFERENCE KEY FrameMonitor ALREADY READS. The page
// stores values; it never captures, never writes a bundle and never deletes
// one itself.

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class SettingsManager;

class PerfSettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PerfSettingsWidget(SettingsManager *settings, QWidget *parent = nullptr);

    /// Persists perf/captureSeconds, perf/captureRoot, perf/keepDays and
    /// perf/keepBytes. Every one takes effect at the NEXT capture.
    void saveSettings();

private:
    void refreshResolved();

    SettingsManager *mSettings = nullptr;
    QDoubleSpinBox  *mSeconds = nullptr;
    QLineEdit       *mRoot = nullptr;
    QSpinBox        *mKeepDays = nullptr;
    QSpinBox        *mKeepGb = nullptr;
    QLabel          *mResolved = nullptr;
};

#endif // PERFSETTINGSWIDGET_H
