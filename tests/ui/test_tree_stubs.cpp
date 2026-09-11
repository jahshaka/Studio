/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for ui.tree_multiselect. The outliner panel reaches the shell
// (delete/duplicate/export/material), the scene writer and the Studio database
// through MENU ROWS the suite never opens — it clicks rows in column 0 and
// reads the set that comes out. Stubbing them keeps the suite from linking the
// whole shell and half of io/.

#include <QJsonObject>
#include <QString>

#include "data/database/database.h"
#include "io/scenewriter.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"

// ---- the shell -------------------------------------------------------------
void MainWindow::deleteNode() {}
void MainWindow::duplicateNode() {}
void MainWindow::createMaterial() {}
void MainWindow::exportNode(const iris::SceneNodePtr &, ModelTypes) {}

// ---- the scene-edit service (menu rows only) -------------------------------
bool SceneEditService::setDecalTexture(const iris::DecalNodePtr &, const QString &) { return false; }
// The inline editor's commit (node.rename's edit) — the suite never opens an editor.
QString SceneEditService::renameNode(const iris::SceneNodePtr &, const QString &) { return {}; }

// ---- the clipboard (menu rows only) ----------------------------------------
//
// The outliner's Cut/Copy/Paste rows call ClipboardService, which reaches a
// database, the CAS and the import path — none of which this suite links, and
// none of which it exercises (it clicks rows in column 0). The ENVELOPE codec
// is linked for real (io/clipboardformat.cpp is a leaf) because the menu asks
// it whether the payload holds anything pasteable before offering the row.
#include "services/clipboardservice.h"

clipboardformat::Envelope ClipboardService::contents() const { return {}; }
ClipboardCopyResult ClipboardService::copyNodes(const QList<iris::SceneNodePtr> &) { return {}; }
ClipboardCutResult ClipboardService::cutNodes(const QList<iris::SceneNodePtr> &) { return {}; }
ClipboardPasteResult ClipboardService::paste(const ClipboardPasteOptions &) { return {}; }

// ---- reparent command's refresh/selection calls ----------------------------
// The suite never drops a row onto another, so the reparent command never runs;
// SelectionService itself is left out of the link on purpose (its moc, compiled
// beside the panel's, hits Qt's automatic QSharedPointer metatype declaration
// before scenenode.h's explicit one).
#include "services/selectionservice.h"
void SceneEditService::notifyHierarchyChanged() {}
void SceneEditService::notifyTransformChanged() {}   // SetNodePropertyCommand (the outliner eye, 2026-09-12)
void SelectionService::select(iris::SceneNodePtr) {}
