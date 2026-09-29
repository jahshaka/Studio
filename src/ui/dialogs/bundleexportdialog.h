/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BUNDLEEXPORTDIALOG_H
#define BUNDLEEXPORTDIALOG_H

// THE EXPORT A PERSON WATCHES (EXPORT-THREAD-1). Every UI export door — the
// Assets tray's four rows, the Materials module's Export material, the
// outliner's Export Object — writes its share file through the same staged job
// the verbs use (services/bundlewriter.h), behind the app's progress dialog:
// the write runs on a worker, the window keeps drawing, the dialog counts the
// files and Cancel stops the write (nothing is left at the destination — a
// file already there is replaced only by a complete archive).

#include <QString>

#include "services/bundlewriter.h"

class QWidget;

namespace bundleexportdialog {

/// Writes `stage` to `destPath` on a worker behind a modal progress dialog
/// titled `title`, and returns when the file is complete (or the user
/// cancelled it). The dialog appears only if the write outlasts a moment, so
/// a small export never flashes one.
assetshare::ExportResult run(QWidget *parent, assetshare::BundleStage stage,
                             const QString &destPath, const QString &title);

}   // namespace bundleexportdialog

#endif   // BUNDLEEXPORTDIALOG_H
