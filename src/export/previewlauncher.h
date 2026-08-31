/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PREVIEWLAUNCHER_H
#define PREVIEWLAUNCHER_H

// PreviewLauncher — WEB_EXPORT_AUDIT §7.6: launch the exported viewer in a
// detected Chromium-family browser as a chromeless `--app` companion window
// (WebGPU works there flagless from file://, verified §7.3), with plain
// open-in-default-browser as the always-available floor (§7.4). Never embeds,
// never reparents (§7.3 "do not attempt").

#include <QString>

class QObject;
class QProcess;

class PreviewLauncher
{
public:
    /// First Chromium-family browser on this system, or empty. Linux: PATH
    /// probes; macOS: app-bundle paths; Windows: PATH (msedge always exists).
    static QString findChromiumBrowser();

    /// Launches `indexHtml` in a kiosk-style `--app` window of the detected
    /// browser, isolated from the user's profile via --user-data-dir inside
    /// the export folder. Returns the owned QProcess (parented to `parent`;
    /// caller terminates it to close the preview), or nullptr when no
    /// Chromium-family browser exists.
    static QProcess *launchKiosk(const QString &indexHtml, QObject *parent);

    /// The floor: open in the default browser via QDesktopServices.
    static bool openInBrowser(const QString &indexHtml);
};

#endif // PREVIEWLAUNCHER_H
