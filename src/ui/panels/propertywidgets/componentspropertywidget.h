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
// THE SUBJECT IS THE GROUP, NOT THE SELECTION, AND IT IS STICKY. Clicking a
// part makes the part the selected node, and if the section then re-read "the
// selected node's children" it would empty itself and disappear under the
// user's cursor.
//
// Two rules keep it on screen, and BOTH are needed. `subjectFor()` climbs out
// of an asset's parts (`attached`) to the node the outliner draws — which
// answers for an imported model, whose parts are attached by the importer. It
// does NOT answer for a group a user made by hand out of plain children: those
// are not attached, so a clicked child resolves to ITSELF, has no parts, and
// the section went away exactly as described above (send-back item 1). So the
// widget also KEEPS the subject it is already showing whenever the node it is
// handed is that subject or a descendant of it. A selection that leaves the
// group entirely drops it; a null node clears it outright.
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

    /// THE GROUP a selected node belongs to, IGNORING what is on screen:
    /// climb while the node is one of its parent's ASSET PARTS (`attached`),
    /// stopping at the node the outliner draws. A node that is not a part
    /// answers itself. The cold half of the rule — see appliesTo() for the
    /// sticky half, which is the one the panel asks.
    static iris::SceneNodePtr subjectFor(const iris::SceneNodePtr &node);
    /// WHETHER THIS SELECTION KEEPS THE SECTION ON SCREEN — the panel's one
    /// question, and an INSTANCE question because the answer depends on what
    /// the list is already showing: a plain child of a hand-made group has no
    /// parts of its own and would fail the cold test, but it is a row of the
    /// list in front of the user and clicking it must not make the list
    /// vanish. True when the node's own group has parts, or when the node is
    /// the subject already on screen or one of its descendants.
    bool appliesTo(const iris::SceneNodePtr &node) const;
    /// The cold form of the same question, for a caller with no widget.
    static bool applies(const iris::SceneNodePtr &node);

    /// The group the list is showing (null when it is showing nothing) and how
    /// many parts it holds — read by ui.components to prove that a selection
    /// leaving the group DROPS the subtree rather than keeping it alive.
    iris::SceneNodePtr subjectNode() const { return subject; }
    int partCount() const { return partsByGuid.size(); }

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
    /// can be answered with a repaint instead of a rebuild. O(parts): a
    /// five-thousand-part import pays about a millisecond per pick to find out
    /// that nothing changed, which is the trade this shape makes on purpose
    /// (the alternative is a document-side change feed the panel does not have).
    QString listSignature() const;
    /// True when `node` IS `subject` or sits under it — the sticky rule's walk.
    bool holdsNode(const iris::SceneNodePtr &node) const;
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
