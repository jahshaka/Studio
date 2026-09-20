/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef COMPONENTSPROPERTYWIDGET_H
#define COMPONENTSPROPERTYWIDGET_H

// COMPONENTS — the parts of a grouped node (owner review R14, COMPONENTS-1).
//
// "we need to add the ability to view individual components of a grouped asset
// like the objects we imported in the right column."
//
// An imported model is ONE row in the outliner with its meshes hidden beneath
// it (they are `attached`, and the outliner skips them on purpose — a
// two-hundred-part model would bury the scene). This section is where those
// parts become reachable: every one listed with its type icon and its own
// visibility and lock state, a click selects it, Shift/Ctrl adds to the
// selection, a double-click frames it in the viewport, and the list highlights
// whatever the hierarchy has selected.
//
// THE SUBJECT IS THE GROUP, NOT THE SELECTION. Clicking a part makes the part
// the selected node, and if the section then re-read "the selected node's
// children" it would empty itself and disappear under the user's cursor. So
// the panel hands it the GROUP: `subjectFor()` climbs out of an asset's parts
// to the node the outliner actually draws, and the list stays put with the
// clicked part highlighted.
//
// READ-ONLY INDICATORS. The eye and the padlock here say what the state IS;
// the hierarchy owns the toggles (its rows edit them, undoably, with the folder
// and cascade rules this list has no business repeating). The section's tooltip
// says so.

#include <QHash>
#include <QPointer>
#include <QSharedPointer>
#include <QStringList>

#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"

class IEditorViewport;
class QTreeWidget;
class QTreeWidgetItem;
struct StudioServices;

class ComponentsPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    ComponentsPropertyWidget();

    /// The node whose parts to list — hand it the SELECTED node; the widget
    /// resolves the group itself (see subjectFor).
    void setSceneNode(iris::SceneNodePtr sceneNode);
    void setServices(StudioServices *s);
    void setSceneView(IEditorViewport *view) { sceneView = view; }

    /// THE GROUP a selected node belongs to: climb while the node is one of its
    /// parent's ASSET PARTS (`attached`), stopping at the node the outliner
    /// draws. A node that is not a part answers itself. Static and public
    /// because the panel asks it too — "does this selection have a Components
    /// section at all" and "which list does it show" must be the one question.
    static iris::SceneNodePtr subjectFor(const iris::SceneNodePtr &node);
    /// Whether a selection warrants the section: its group has parts.
    static bool applies(const iris::SceneNodePtr &node);

    /// HOW MANY TIMES THE LIST WAS REBUILT FROM SCRATCH, and how many times a
    /// call only repainted the highlight. The section re-lays BY DIFFERENCE
    /// (SELECT-COST-1's law for the column applies to its rows): moving the
    /// selection inside one model must not destroy and rebuild two hundred
    /// rows. Read by ui.components; never used to make a decision.
    int rebuildCount() const { return rebuilds; }
    int refreshCount() const { return refreshes; }
    /// The rows the list is showing, top to bottom, as "<depth>:<name>".
    QStringList rowLabels() const;
    QTreeWidget *listWidget() const { return tree; }

private slots:
    /// The list's own selection changed BY THE USER — push it to the editor.
    void listSelectionChanged();
    void rowActivated(QTreeWidgetItem *item, int column);

private:
    /// Repaints the rows' highlight from the editor selection, quietly.
    void applyEditorSelection();
    /// The list's current contents as a string, so a call that changes nothing
    /// can be answered with a repaint instead of a rebuild.
    QString listSignature() const;
    /// Rows the user could scroll to — every row, minus those inside a folded
    /// one. What the list's height is sized from.
    int visibleRowCount() const;

    QTreeWidget *tree = nullptr;
    /// The group the list belongs to (what subjectFor resolved).
    iris::SceneNodePtr subject;
    /// The parts by guid, filled when the rows are built — so a click resolves
    /// a row to its node with a lookup and not with a walk of the model per
    /// selected row.
    QHash<QString, iris::SceneNodePtr> partsByGuid;
    StudioServices *services = nullptr;
    IEditorViewport *sceneView = nullptr;
    /// The signature the rows were last built from.
    QString builtSignature;
    /// True while the widget is writing its own selection — the guard that
    /// stops "the editor told us" from being pushed straight back as "the user
    /// clicked", which is an endless round trip between two selection models.
    bool applying = false;
    int rebuilds = 0;
    int refreshes = 0;
};

#endif   // COMPONENTSPROPERTYWIDGET_H
