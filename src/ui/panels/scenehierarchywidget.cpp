/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/scenehierarchywidget.h"
#include "ui_scenehierarchywidget.h"

#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QInputDialog>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QItemSelectionModel>
#include <QUndoStack>

#include "commands/reparentscenenodecommand.h"
#include "commands/scenefoldercommand.h"
#include "services/scenefolders.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/core/irisutils.h"
#include "shell/mainwindow.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/clipboardservice.h"
#include "data/constants.h"
#include "viewport/ieditorviewport.h"
#include "services/planarreflectors.h"
#include <QMessageBox>
#include "bridge/enginehost.h"
#include "io/scenewriter.h"
#include <qdialog.h>
#include <qcombobox.h>
#include <QBrush>
#include "ui/style/stylesheet.h"

SceneHierarchyWidget::SceneHierarchyWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SceneHierarchyWidget)
{
    ui->setupUi(this);
    // scenehierarchywidget.ui used to embed these (classic-only now; theme sweep)
    setStyleSheet(StyleSheet::SceneHierarchyRoot());
    ui->widget->setStyleSheet(StyleSheet::SceneHierarchyWidget());
    ui->sceneTree->setStyleSheet(StyleSheet::SceneHierarchySceneTree());

    mainWindow = nullptr;

	ui->sceneTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	ui->sceneTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ui->sceneTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	ui->sceneTree->header()->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->sceneTree->viewport()->installEventFilter(this);
    ui->sceneTree->setItemDelegate(new TreeItemDelegate(this));

    ui->sceneTree->setAlternatingRowColors(true);

    connect(ui->sceneTree->itemDelegate(), &QAbstractItemDelegate::commitData, this, &SceneHierarchyWidget::OnLstItemsCommitData);

    ui->sceneTree->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->sceneTree->viewport()->setAttribute(Qt::WA_MacShowFocusRect, false);

	connect(ui->sceneTree,	SIGNAL(itemClicked(QTreeWidgetItem*, int)),
			this,			SLOT(treeItemSelected(QTreeWidgetItem*, int)));

    // Multi-select (SCENEGRAPH_SPEC §6b): shift/ctrl picks a SET, which is what
    // "folder-ise these five objects" and "delete these five objects" need. The
    // LAST row selected still drives the properties panel and the gizmo — that
    // is treeSelectionChanged(), which reads currentItem() rather than the set.
    ui->sceneTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->sceneTree->setDragEnabled(true);
    ui->sceneTree->viewport()->setAcceptDrops(true);
    ui->sceneTree->setDropIndicatorShown(false);
	ui->sceneTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->sceneTree->setDragDropMode(QAbstractItemView::InternalMove);

    ui->sceneTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->sceneTree,	SIGNAL(customContextMenuRequested(const QPoint&)),
			this,			SLOT(sceneTreeCustomContextMenu(const QPoint&)));

    // Keyboard navigation and shift-select have to drive the properties panel
    // too — itemClicked alone only ever saw the mouse.
    connect(ui->sceneTree, &QTreeWidget::itemSelectionChanged,
            this, &SceneHierarchyWidget::treeSelectionChanged);

    connect(ui->folderBtn, &QPushButton::clicked,
            this, &SceneHierarchyWidget::newFolderFromSelection);

	// We do QIcon::Selected manually to remove an annoying default highlight for selected icons
	visibleIcon = new QIcon;
	visibleIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48.png"), QIcon::Normal);
	visibleIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48.png"), QIcon::Selected);

	hiddenIcon = new QIcon;
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48-dim.png"), QIcon::Normal);
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48-dim.png"), QIcon::Selected);

    pickableIcon = new QIcon;
    pickableIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/lock-dim.png"), QIcon::Normal);
    pickableIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/lock-dim.png"), QIcon::Selected);

    disabledIcon = new QIcon;
    disabledIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/lock-filled.png"), QIcon::Normal);
    disabledIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/lock-filled.png"), QIcon::Selected);

    ui->sceneTree->setStyleSheet(StyleSheet::SceneHierarchyTree());
}

void SceneHierarchyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    //todo: cleanly remove previous scene
    this->scene = scene;
    this->repopulateTree();
}

void SceneHierarchyWidget::setMainWindow(MainWindow *mainWin)
{
    mainWindow = mainWin;

    QMenu* addMenu = new QMenu();
	addMenu->setStyleSheet(StyleSheet::QMenuDark());

	// Primitives
    auto primtiveMenu = addMenu->addMenu("Primitive");

    QAction *action = new QAction("Torus", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addTorus()));

    action = new QAction("Cube", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addCube()));

    action = new QAction("Sphere", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addSphere()));

    action = new QAction("Cylinder", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addCylinder()));

    action = new QAction("Plane", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addPlane()));

    action = new QAction("Ground", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addGround()));

    // Lamps
    auto lightMenu = addMenu->addMenu("Light");
    action = new QAction("Point", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addPointLight()));

    action = new QAction("Spot", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addSpotLight()));

    action = new QAction("Directional", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addDirectionalLight()));

    action = new QAction("Area", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addAreaLight()));

    // Decal (DECALS_SPEC): a top-level entry, not under Light — it is its own
    // object kind. It spawns without an image; the Decal panel's picker (or a
    // drop from the asset bin) binds one.
    action = new QAction("Decal", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addDecal()));

    action = new QAction("Empty", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addEmpty()));

    // Scene cameras (CAMERAS_SPEC phase 1) — the verb existed before this row
    // did; a capability with no UI entry point is invisible to most users
    // (owner sighting 2026-09-06).
    action = new QAction("Camera", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addCamera()));

    // Systems
    action = new QAction("Particle System", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addParticleSystem()));

    ui->addBtn->setMenu(addMenu);
    ui->addBtn->setPopupMode(QToolButton::InstantPopup);

    connect(ui->deleteBtn, SIGNAL(clicked(bool)), mainWindow, SLOT(deleteNode()));
}

void SceneHierarchyWidget::setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    selectedNode = sceneNode;

    if (!!sceneNode) {
        auto item = treeItemList.value(sceneNode->getNodeId());
        if (!item) return;
        // The panel drives the viewport and the viewport drives the panel. With
        // the tree on ExtendedSelection, setCurrentItem now raises
        // itemSelectionChanged, so without this guard a pick in the viewport
        // would bounce straight back out as a selection the viewport just made.
        suppressSelectionSignal = true;
        ui->sceneTree->setCurrentItem(item);
        suppressSelectionSignal = false;
		ui->sceneTree->scrollTo(ui->sceneTree->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
}

bool SceneHierarchyWidget::isFolderItem(const QTreeWidgetItem *item)
{
    return item && !item->data(0, kFolderRole).toString().isEmpty();
}

QString SceneHierarchyWidget::folderPathOf(const QTreeWidgetItem *item)
{
    return item ? item->data(0, kFolderRole).toString() : QString();
}

QList<iris::SceneNodePtr> SceneHierarchyWidget::selectedNodes() const
{
    QList<iris::SceneNodePtr> out;
    const auto rows = ui->sceneTree->selectedItems();
    for (QTreeWidgetItem *row : rows) {
        if (isFolderItem(row)) continue;
        const auto node = nodeList.value(row->data(0, Qt::UserRole).toLongLong());
        if (!!node && !out.contains(node)) out.append(node);
    }
    return out;
}

// ---- MULTI-SELECTION (EDITOR_MULTISELECT_SPEC §2.2) ------------------------

bool SceneHierarchyWidget::isWorldRoot(const iris::SceneNodePtr &node) const
{
    return !!node && ((!!scene && scene->getRootNode() == node) || node->isRootNode());
}

QList<iris::SceneNodePtr> SceneHierarchyWidget::visibleNodeRows() const
{
    QList<iris::SceneNodePtr> rows;
    // VISIBLE rows, in the order they are DRAWN. QTreeWidgetItemIterator's
    // NotHidden flag covers hidden items; a collapsed subtree's children are
    // not "hidden" items, so the ancestor test below is what excludes them —
    // and it has to, because "everything between these two rows" means what is
    // on screen, not what is in the document.
    for (QTreeWidgetItemIterator it(ui->sceneTree, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
        QTreeWidgetItem *row = *it;
        if (isFolderItem(row)) continue;              // a folder is a ROW, never a node
        bool collapsedAncestor = false;
        for (QTreeWidgetItem *p = row->parent(); p; p = p->parent())
            if (!p->isExpanded()) { collapsedAncestor = true; break; }
        if (collapsedAncestor) continue;
        const auto node = nodeList.value(row->data(0, Qt::UserRole).toLongLong());
        if (!node) continue;
        rows.append(node);
    }
    return rows;
}

QList<iris::SceneNodePtr>
SceneHierarchyWidget::nodesInVisibleRange(const iris::SceneNodePtr &a,
                                          const iris::SceneNodePtr &b) const
{
    QList<iris::SceneNodePtr> out;
    if (!a || !b) return out;

    const QList<iris::SceneNodePtr> rows = visibleNodeRows();
    int i = -1, j = -1;
    for (int k = 0; k < rows.size(); ++k) {
        if (rows[k].data() == a.data()) i = k;
        if (rows[k].data() == b.data()) j = k;
    }
    if (i < 0 || j < 0) return out;
    if (i > j) std::swap(i, j);

    for (int k = i; k <= j; ++k) {
        if (isWorldRoot(rows[k])) continue;           // D6
        if (!out.contains(rows[k])) out.append(rows[k]);
    }
    return out;
}

// D1(b) — THE OWNER'S SHIFT RULE, and the one place it lives.
//
// "Shift-click should select all items from the TOPMOST SELECTED one to the
// last one I Shift-click." That differs from Qt, Unreal and Blender in exactly
// one case: after Ctrl-clicks that left the last-clicked row BELOW an
// earlier-selected one (rows 5 and 8 selected, Shift+12 gives {5..12} here and
// {8..12} everywhere else). Qt applies ITS range on the press, so this runs
// from the press-time event filter and CONSUMES the event — otherwise the
// panel would flash Qt's selection first and ours a moment later.
void SceneHierarchyWidget::applyShiftRange(const iris::SceneNodePtr &clicked, bool additive)
{
    if (!clicked) return;
    const QList<iris::SceneNodePtr> rows = visibleNodeRows();
    if (rows.isEmpty()) return;

    // The ANCHOR is the topmost selected VISIBLE row — not stored state, and
    // not the current item (D2: the primary is the last clicked, the anchor is
    // derived). With nothing selected, a Shift+click is a plain click.
    QList<iris::SceneNodePtr> current = selectedNodes();
    iris::SceneNodePtr anchor;
    for (const auto &row : rows) {
        if (isWorldRoot(row)) continue;
        if (current.contains(row)) { anchor = row; break; }
    }
    if (!anchor) anchor = clicked;

    QList<iris::SceneNodePtr> range = nodesInVisibleRange(anchor, clicked);
    if (range.isEmpty()) return;

    // Ctrl+Shift extends instead of replacing (Blender's additive range).
    QList<iris::SceneNodePtr> members;
    if (additive) {
        for (const auto &n : current) if (!isWorldRoot(n)) members.append(n);
    }
    for (const auto &n : range) if (!members.contains(n)) members.append(n);

    // The CLICKED row is the primary — every system makes the clicked row
    // current, and the properties panel has to follow the mouse.
    QList<iris::SceneNodePtr> ordered;
    ordered.append(clicked);
    for (const auto &n : members) if (n.data() != clicked.data()) ordered.append(n);

    setSelectedSet(ordered);              // paint it now; the guard stops the echo
    announceSet(ordered);
}

// One place decides whether a set is worth announcing, and in what shape.
void SceneHierarchyWidget::announceSet(const QList<iris::SceneNodePtr> &nodes)
{
    QList<qint64> ids;
    for (const auto &n : nodes) if (n) ids.append(n->getNodeId());
    if (ids == lastAnnouncedSet) return;
    lastAnnouncedSet = ids;
    selectedNode = nodes.isEmpty() ? iris::SceneNodePtr() : nodes.first();
    if (nodes.size() > 1) emit sceneNodeSetSelected(nodes);
    else                  emit sceneNodeSelected(selectedNode);
}

void SceneHierarchyWidget::setSelectedSet(const QList<iris::SceneNodePtr> &nodes)
{
    selectedNode = nodes.isEmpty() ? iris::SceneNodePtr() : nodes.first();

    suppressSelectionSignal = true;
    ui->sceneTree->clearSelection();
    QTreeWidgetItem *primaryItem = nullptr;
    for (const auto &node : nodes) {
        if (!node) continue;
        auto *item = treeItemList.value(node->getNodeId());
        if (!item) continue;
        item->setSelected(true);
        if (!primaryItem) primaryItem = item;
    }
    if (primaryItem) {
        ui->sceneTree->setCurrentItem(primaryItem, 0,
                                      QItemSelectionModel::NoUpdate);
        ui->sceneTree->scrollTo(ui->sceneTree->currentIndex(),
                                QAbstractItemView::PositionAtCenter);
    }
    suppressSelectionSignal = false;
}

bool SceneHierarchyWidget::isFolderable(const iris::SceneNodePtr &node) const
{
    // Folders organise the ROOT LEVEL of the outliner (§6b, Unreal semantics):
    // a node that hangs off another node is displayed under its parent, so
    // filing it would record metadata nothing ever shows. Refused, visibly,
    // rather than silently accepted.
    if (!node || !scene) return false;
    auto root = scene->getRootNode();
    return !!root && node != root && node->getParent() == root;
}

void SceneHierarchyWidget::treeSelectionChanged()
{
    if (suppressSelectionSignal) return;
    auto *current = ui->sceneTree->currentItem();
    // A folder row carries no node: the properties panel keeps showing whatever
    // it had rather than going blank, which is what selecting a container does
    // everywhere else in the app.
    if (!current || isFolderItem(current)) return;
    const auto node = nodeList.value(current->data(0, Qt::UserRole).toLongLong());
    if (!node) return;

    // THE WHOLE SET, not currentItem() alone (EDITOR_MULTISELECT_SPEC §2.2).
    // Qt's own Ctrl-toggle and plain click already produce the right set here;
    // the only rule this panel has to impose is Shift's (applyShiftRange, on
    // the press) and D6's — the World root is never a member of a multi, so a
    // set that contains it plus something else drops it.
    QList<iris::SceneNodePtr> set = selectedNodes();
    if (set.size() > 1) {
        QList<iris::SceneNodePtr> filtered;
        for (const auto &n : set) if (!isWorldRoot(n)) filtered.append(n);
        set = filtered;
    }
    // NOTHING SELECTED IS AN ANSWER (smoke S3, owner: "clicking the empty
    // space under the list deselects in the list but the node stays selected
    // in the viewport"). An empty set used to be REPLACED by the current row —
    // so Ctrl-clicking the last selected row re-announced it, and the panel and
    // the viewport could never agree on "nothing". The current row survives as
    // Qt's cursor; it is simply not a selection any more.
    if (set.isEmpty()) { announceSet({}); return; }

    // The clicked/current row is the PRIMARY.
    QList<iris::SceneNodePtr> ordered;
    if (set.contains(node)) ordered.append(node);
    for (const auto &n : set) if (n.data() != node.data()) ordered.append(n);
    if (ordered.isEmpty()) return;

    announceSet(ordered);
}

bool SceneHierarchyWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Drag-to-reparent, guarded (ASSET_ADD_AUDIT D2): only a drag that actually
    // originates on this tree may reparent (mime + source check — a foreign drag
    // Qt routes in must never move the current selection), descendant targets
    // are refused (they would create a parent cycle — infinite recursion in
    // getGlobalTransform), and the reparent goes through the undo stack.
    //
    // TWO drops share this tree since folders landed (SCENEGRAPH_SPEC §6b):
    // dropping ON A NODE row still reparents, exactly as before; dropping on a
    // FOLDER row, or BETWEEN root-level rows, is a folderPath metadata edit that
    // never touches the hierarchy. dropHintAt() is the single place that decides
    // which, and it also feeds the drop indicator so the two read differently on
    // screen before the mouse is released.
    static const char *kTreeMime = "application/x-qabstractitemmodeldatalist";

    // SHIFT+PRESS: OURS, not Qt's (EDITOR_MULTISELECT_SPEC §2.2/D1 b).
    //
    // With ExtendedSelection Qt applies its OWN range — anchored on the current
    // item — inside mousePressEvent and fires itemSelectionChanged
    // synchronously. The owner's rule anchors on the TOPMOST SELECTED row
    // instead, so the press has to be intercepted and consumed here; letting Qt
    // run first and correcting afterwards would show two different selections
    // in one click. Ctrl+click and the plain click stay Qt's — those already
    // match the rule.
    if (watched == ui->sceneTree->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        // A CLICK ON EMPTY SPACE DESELECTS EVERYTHING (smoke S3). Qt clears its
        // own selection for us, but it also clears the CURRENT item, and
        // treeSelectionChanged's one path out starts with `if (!current)
        // return` — so the clear was never announced and SelectionService (the
        // properties panel, the viewport outline, the gizmo) kept the node the
        // user had just deselected. Announced here, where "the user clicked
        // nothing" is still known, through the same empty set `editor.select(null)`
        // produces. Modified clicks are Qt's: Ctrl/Shift on empty space are
        // range/toggle gestures, not a clear.
        if (me->button() == Qt::LeftButton
            && !(me->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
            && !ui->sceneTree->itemAt(me->position().toPoint())) {
            setSelectedSet({});                       // paints the clear
            suppressSelectionSignal = true;
            ui->sceneTree->setCurrentItem(nullptr);    // ...and drops the cursor row
            suppressSelectionSignal = false;
            announceSet({});
            return false;   // Qt still gets the press (focus, the context menu)
        }
        if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::ShiftModifier)) {
            const QPoint pos = me->position().toPoint();
            // Column 0 only: the eye and lock columns stay per-row toggles.
            if (ui->sceneTree->columnAt(pos.x()) == 0) {
                QTreeWidgetItem *row = ui->sceneTree->itemAt(pos);
                const auto node = (row && !isFolderItem(row))
                    ? nodeList.value(row->data(0, Qt::UserRole).toLongLong())
                    : iris::SceneNodePtr();
                if (node && !node->isRootNode()) {
                    applyShiftRange(node, me->modifiers() & Qt::ControlModifier);
                    return true;      // consumed: Qt's range must not also run
                }
            }
        }
    }

    if (event->type() == QEvent::DragEnter) {
        auto evt = static_cast<QDragEnterEvent*>(event);
        lastDraggedNodes.clear();
        const bool internal = evt->source() == ui->sceneTree ||
                              evt->source() == ui->sceneTree->viewport();
        // ASSET BIN -> DECAL is the ONE foreign drag this tree accepts
        // (DECALS_SPEC §5.6). Everything else foreign stays refused: the
        // reparent guard exists because Qt routes stray drags in here.
        if (!internal && evt->mimeData() && evt->mimeData()->hasFormat(kTreeMime))
            evt->acceptProposedAction();
        if (internal && evt->mimeData() && evt->mimeData()->hasFormat(kTreeMime)) {
            // The drag carries the WHOLE selection now, not just the pressed
            // row (the old single-node member is gone with it).
            lastDraggedNodes = selectedNodes();
        }
    }

    if (event->type() == QEvent::DragLeave) {
        ui->sceneTree->clearDropHint();
    }

    if (event->type() == QEvent::DragMove) {
        // Qt only delivers a Drop when DragMove is accepted.
        auto evt = static_cast<QDragMoveEvent*>(event);
        const bool internal = evt->source() == ui->sceneTree ||
                              evt->source() == ui->sceneTree->viewport();
        if (!internal && evt->mimeData() && evt->mimeData()->hasFormat(kTreeMime)) {
            QTreeWidgetItem *row = ui->sceneTree->itemAt(evt->position().toPoint());
            iris::SceneNodePtr target =
                row ? nodeList.value(row->data(0, Qt::UserRole).toLongLong()) : iris::SceneNodePtr();
            if (target && target->getSceneNodeType() == iris::SceneNodeType::Decal) {
                evt->acceptProposedAction();
                return true;
            }
        }
        if (internal && !lastDraggedNodes.isEmpty()) {
            QTreeWidgetItem *row = nullptr;
            const auto hint = dropHintAt(lastDraggedNodes, evt->position().toPoint(), &row, nullptr);
            ui->sceneTree->setDropHint(hint, row);
            if (hint != SceneTreeWidget::DropHint::None) {
                evt->acceptProposedAction();
                return true;
            }
            evt->ignore();
            return true;
        }
    }

    if (event->type() == QEvent::Drop) {
        auto dropEventPtr = static_cast<QDropEvent*>(event);
        ui->sceneTree->clearDropHint();
        auto dragged = lastDraggedNodes;
        lastDraggedNodes.clear();

        const bool internal = dropEventPtr->source() == ui->sceneTree ||
                              dropEventPtr->source() == ui->sceneTree->viewport();

        // ASSET BIN -> DECAL: bind a Texture asset as the decal's image.
        if (!internal && dropEventPtr->mimeData() &&
            dropEventPtr->mimeData()->hasFormat(kTreeMime) && mainWindow) {
            QTreeWidgetItem *row = ui->sceneTree->itemAt(dropEventPtr->position().toPoint());
            iris::SceneNodePtr target =
                row ? nodeList.value(row->data(0, Qt::UserRole).toLongLong()) : iris::SceneNodePtr();
            if (target && target->getSceneNodeType() == iris::SceneNodeType::Decal) {
                QByteArray encoded = dropEventPtr->mimeData()->data(kTreeMime);
                QDataStream stream(&encoded, QIODevice::ReadOnly);
                QMap<int, QVariant> roleDataMap;
                while (!stream.atEnd()) stream >> roleDataMap;
                if (roleDataMap.value(0).toInt() == static_cast<int>(ModelTypes::Texture)) {
                    mainWindow->studioServices()->sceneEdit->setDecalTexture(
                        target.staticCast<iris::DecalNode>(), roleDataMap.value(3).toString());
                    dropEventPtr->acceptProposedAction();
                    return true;
                }
            }
        }

        if (!internal || !dropEventPtr->mimeData() ||
            !dropEventPtr->mimeData()->hasFormat(kTreeMime) || dragged.isEmpty()) {
            dropEventPtr->setDropAction(Qt::IgnoreAction);
            dropEventPtr->ignore();
            return true;   // consume: a foreign drag must not touch the tree
        }

        QTreeWidgetItem *row = nullptr;
        QString folder;
        const auto hint = dropHintAt(dragged, dropEventPtr->position().toPoint(), &row, &folder);

        // FOLDER DROPS are metadata only — one snapshot command, one undo step,
        // and NOT ONE NODE MOVES IN THE HIERARCHY (§6b LAW).
        if (hint == SceneTreeWidget::DropHint::IntoFolder ||
            hint == SceneTreeWidget::DropHint::ToRoot) {
            QList<iris::SceneNodePtr> movable;
            for (const auto &n : dragged) if (isFolderable(n)) movable.append(n);
            moveNodesToFolder(movable, folder);
            dropEventPtr->setDropAction(Qt::IgnoreAction);
            dropEventPtr->accept();
            return true;
        }

        iris::SceneNodePtr target;
        if (row && !isFolderItem(row))
            target = nodeList.value(row->data(0, Qt::UserRole).toLongLong());

        if (hint != SceneTreeWidget::DropHint::Reparent || !target) {
            dropEventPtr->setDropAction(Qt::IgnoreAction);
            dropEventPtr->ignore();
            return true;
        }

        // REPARENT, unchanged in every respect except that it now moves the
        // whole selection. Each node is re-checked against the same two guards
        // (already-the-parent, cycle) — a multi-selection can easily contain
        // both a legal and an illegal member.
        QList<iris::SceneNodePtr> moves;
        for (const auto &n : dragged) {
            if (!n || n == target) continue;
            if (n->getParent() == target) continue;
            if (ReparentSceneNodeCommand::wouldCreateCycle(n, target)) continue;
            moves.append(n);
        }
        if (moves.isEmpty() || !mainWindow) {
            dropEventPtr->setDropAction(Qt::IgnoreAction);
            dropEventPtr->ignore();
            return true;
        }

        auto *undo = mainWindow->studioServices()->undo;
        // One GESTURE is one undo step even when it moved five objects.
        const bool macro = moves.size() > 1 && undo->stack();
        if (macro) undo->stack()->beginMacro(tr("Reparent Objects"));
        for (const auto &n : moves) undo->push(new ReparentSceneNodeCommand(n, target));
        if (macro) undo->stack()->endMacro();

        // The command repopulated the tree from the document. Consume the event
        // so QTreeWidget's own InternalMove does not run on the rebuilt tree.
        dropEventPtr->setDropAction(Qt::IgnoreAction);
        dropEventPtr->accept();
        return true;
    }

    return QObject::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Outliner folders (SCENEGRAPH_SPEC §6b)
// ---------------------------------------------------------------------------

SceneTreeWidget::DropHint SceneHierarchyWidget::dropHintAt(const QList<iris::SceneNodePtr> &dragged,
                                                           const QPoint &pos,
                                                           QTreeWidgetItem **rowOut,
                                                           QString *folderOut) const
{
    using Hint = SceneTreeWidget::DropHint;
    if (rowOut) *rowOut = nullptr;
    if (folderOut) folderOut->clear();
    if (!scene) return Hint::None;

    QTreeWidgetItem *row = ui->sceneTree->itemAt(pos);

    // Empty space under the last row: back to the root level.
    if (!row) {
        for (const auto &n : dragged) if (isFolderable(n)) return Hint::ToRoot;
        return Hint::None;
    }
    if (rowOut) *rowOut = row;

    if (isFolderItem(row)) {
        // Onto a folder: file everything filable there.
        for (const auto &n : dragged) if (isFolderable(n)) {
            if (folderOut) *folderOut = folderPathOf(row);
            return Hint::IntoFolder;
        }
        if (rowOut) *rowOut = nullptr;
        return Hint::None;
    }

    const auto target = nodeList.value(row->data(0, Qt::UserRole).toLongLong());
    if (!target) { if (rowOut) *rowOut = nullptr; return Hint::None; }

    // BETWEEN root-level rows: the top and bottom quarter of a row that is
    // itself at the root level of the outliner means "put it beside this", i.e.
    // into the same folder this row is in. This is the gesture Unreal's
    // outliner has and the reason folders can be reached without aiming at the
    // folder header itself.
    const QRect rect = ui->sceneTree->visualItemRect(row);
    const bool edge = rect.isValid() && rect.height() >= 8 &&
                      (pos.y() - rect.top() < rect.height() / 4 ||
                       rect.bottom() - pos.y() < rect.height() / 4);
    if (edge && isFolderable(target)) {
        for (const auto &n : dragged) if (isFolderable(n) && n != target) {
            if (folderOut) *folderOut = scenefolders::normalize(target->folderPath);
            return Hint::ToRoot;   // "beside", drawn as a line — the folder is in folderOut
        }
    }

    // Onto the WORLD row: for a nested node this is the existing reparent to
    // the root node; for a root-level node it means "leave the folder".
    if (target == scene->getRootNode()) {
        bool anyNested = false, anyFiled = false;
        for (const auto &n : dragged) {
            if (!n) continue;
            if (n->getParent() != scene->getRootNode()) anyNested = true;
            else if (!n->folderPath.isEmpty()) anyFiled = true;
        }
        if (anyNested) return Hint::Reparent;
        if (anyFiled) return Hint::ToRoot;
        if (rowOut) *rowOut = nullptr;
        return Hint::None;
    }

    // Onto a node row: the existing reparent, if any dragged node may go there.
    for (const auto &n : dragged) {
        if (!n || n == target) continue;
        if (n->getParent() == target) continue;
        if (ReparentSceneNodeCommand::wouldCreateCycle(n, target)) continue;
        return Hint::Reparent;
    }
    if (rowOut) *rowOut = nullptr;
    return Hint::None;
}

void SceneHierarchyWidget::runFolderEdit(const QString &text, const std::function<bool()> &fn)
{
    if (!scene || !fn) return;
    const auto before = scenefolders::snapshot(scene);
    if (!fn()) return;
    repopulateTree();
    if (mainWindow && mainWindow->studioServices() && mainWindow->studioServices()->undo) {
        auto *cmd = new SceneFolderCommand(text, scene, before);
        cmd->setPanel(this);
        mainWindow->studioServices()->undo->push(cmd);
    }
}

void SceneHierarchyWidget::moveNodesToFolder(const QList<iris::SceneNodePtr> &nodes,
                                             const QString &path)
{
    if (nodes.isEmpty()) return;
    const QString target = scenefolders::normalize(path);
    runFolderEdit(target.isEmpty() ? tr("Move To Root") : tr("Move To Folder"), [&]() {
        bool changed = false;
        for (const auto &n : nodes) {
            if (!isFolderable(n)) continue;
            if (scenefolders::normalize(n->folderPath) == target) continue;
            scenefolders::setNodeFolder(scene, n, target);
            changed = true;
        }
        return changed;
    });
}

QString SceneHierarchyWidget::askFolderName(const QString &title, const QString &initial)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, title, tr("Folder name"),
                                               QLineEdit::Normal, initial, &ok);
    return ok ? name.trimmed() : QString();
}

void SceneHierarchyWidget::newFolderFromSelection()
{
    if (!scene) return;
    const auto selection = selectedNodes();
    QList<iris::SceneNodePtr> members;
    for (const auto &n : selection) if (isFolderable(n)) members.append(n);

    // The new folder is created NEXT TO whatever the selection is already in,
    // so folder-ising things that live in "Props" gives "Props/<new>".
    QString parent;
    if (!members.isEmpty()) parent = scenefolders::normalize(members.first()->folderPath);

    const QString name = askFolderName(members.isEmpty() ? tr("New Folder")
                                                         : tr("Group In New Folder"),
                                       tr("New Folder"));
    if (name.isEmpty()) return;
    const QString leaf = scenefolders::normalize(name);
    if (leaf.isEmpty() || leaf.contains(QLatin1Char('/'))) return;
    const QString path = parent.isEmpty() ? leaf : parent + QLatin1Char('/') + leaf;

    runFolderEdit(tr("New Folder"), [&]() {
        if (!scenefolders::exists(scene, path)) scenefolders::create(scene, path);
        // The selection is FILED, never reparented (§6b LAW).
        for (const auto &n : members) scenefolders::setNodeFolder(scene, n, path);
        return true;
    });

    if (auto *row = folderItemList.value(path)) {
        ui->sceneTree->setCurrentItem(row);
        ui->sceneTree->scrollToItem(row);
    }
}

void SceneHierarchyWidget::buildMoveToMenu(QMenu *parent, const QList<iris::SceneNodePtr> &nodes)
{
    QMenu *moveTo = parent->addMenu(tr("Move to"));
    moveTo->setStyleSheet(StyleSheet::QMenuDarkPadded());

    QList<iris::SceneNodePtr> filable;
    for (const auto &n : nodes) if (isFolderable(n)) filable.append(n);
    moveTo->setEnabled(!filable.isEmpty());
    if (filable.isEmpty()) {
        // Say WHY rather than offering a menu that would do nothing: folders
        // organise the root level, and these nodes hang off another node.
        moveTo->setToolTip(tr("Folders organise root-level objects; these are "
                              "children of another object."));
        return;
    }

    QAction *root = moveTo->addAction(tr("Root"));
    connect(root, &QAction::triggered, this, [this, filable]() {
        moveNodesToFolder(filable, QString());
    });
    moveTo->addSeparator();

    for (const QString &path : scenefolders::all(scene)) {
        QAction *a = moveTo->addAction(path);
        connect(a, &QAction::triggered, this, [this, filable, path]() {
            moveNodesToFolder(filable, path);
        });
    }
    moveTo->addSeparator();
    QAction *fresh = moveTo->addAction(tr("New Folder…"));
    connect(fresh, &QAction::triggered, this, [this, filable]() {
        const QString name = askFolderName(tr("New Folder"), tr("New Folder"));
        if (name.isEmpty()) return;
        const QString leaf = scenefolders::normalize(name);
        if (leaf.isEmpty() || leaf.contains(QLatin1Char('/'))) return;
        moveNodesToFolder(filable, leaf);
    });
}

QTreeWidgetItem *SceneHierarchyWidget::folderItemFor(const QString &path)
{
    const QString p = scenefolders::normalize(path);
    if (p.isEmpty()) return treeItemList.value(scene->getRootNode()->getNodeId());
    if (auto *existing = folderItemList.value(p)) return existing;

    QTreeWidgetItem *parent = folderItemFor(scenefolders::parentOf(p));
    if (!parent) return nullptr;

    auto *item = new QTreeWidgetItem();
    item->setText(0, scenefolders::leafOf(p));
    item->setData(0, kFolderRole, p);
    // Selectable and renamable, NOT draggable: a folder is not a thing you can
    // drop on something else, it is a thing you drop things into.
    item->setFlags((item->flags() | Qt::ItemIsEditable | Qt::ItemIsDropEnabled) &
                   ~Qt::ItemIsDragEnabled);
    QIcon icon;
    icon.addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-folder-72.png"), QIcon::Normal);
    icon.addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-folder-72.png"), QIcon::Selected);
    item->setIcon(0, icon);
    parent->addChild(item);
    folderItemList.insert(p, item);
    return item;
}

void SceneHierarchyWidget::treeItemSelected(QTreeWidgetItem *item, int column)
{
    // A folder row has no node behind it: the eye and lock columns would look
    // up nodeList[0] and dereference a null shared pointer.
    if (isFolderItem(item)) return;

	// Our icons are in the second column
	if (column == 1) {
		if (item->data(1, Qt::UserRole).toBool()) hideItemAndChildren(item);
		else showItemAndChildren(item);
	}
    else if (column == 2) {
        if (item->data(2, Qt::UserRole).toBool()) lockItemAndChildren(item);
        else releaseItemAndChildren(item);
    }
	else {
		// ONE PATH OUT (EDITOR_MULTISELECT_SPEC §3.1). itemSelectionChanged has
		// usually already reported this row — it fires on the press, before the
		// click — and the case it cannot see is a click on the ALREADY current
		// row. This branch used to emit a single node of its own, which walked
		// straight past every set rule: a Ctrl+click on the World row announced
		// the root as the selection even though D6 says the root is never part
		// of a multi. So it goes through the same computation, and announceSet
		// swallows the duplicate when the set has not actually changed (which
		// is what kept one click from rebuilding the properties panel twice).
		treeSelectionChanged();
	}
}

void SceneHierarchyWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->sceneTree->indexAt(pos);
    auto item = index.isValid() ? ui->sceneTree->itemAt(pos) : nullptr;

    // THE ROW MENU (SCENEGRAPH_SPEC §6b). Built as a real, extensible menu on
    // the tree — this is the designated home for future row actions, so every
    // entry below is added the same way and nothing here is a one-off.

    // ---- empty space: the folder actions that need no row ------------------
    if (!item) {
        if (!scene) return;
        QMenu menu;
        menu.setStyleSheet(StyleSheet::QMenuDarkPadded());
        QAction *newFolder = menu.addAction(tr("New Folder…"));
        connect(newFolder, &QAction::triggered, this,
                &SceneHierarchyWidget::newFolderFromSelection);
        menu.exec(ui->sceneTree->mapToGlobal(pos));
        return;
    }

    // ---- a FOLDER row ------------------------------------------------------
    if (isFolderItem(item)) {
        const QString path = folderPathOf(item);
        QMenu menu;
        menu.setStyleSheet(StyleSheet::QMenuDarkPadded());

        QAction *rename = menu.addAction(tr("Rename"));
        connect(rename, &QAction::triggered, this, [this, item]() {
            ui->sceneTree->editItem(item);
        });

        QAction *sub = menu.addAction(tr("New Subfolder…"));
        connect(sub, &QAction::triggered, this, [this, path]() {
            const QString name = askFolderName(tr("New Subfolder"), tr("New Folder"));
            if (name.isEmpty()) return;
            const QString leaf = scenefolders::normalize(name);
            if (leaf.isEmpty() || leaf.contains(QLatin1Char('/'))) return;
            runFolderEdit(tr("New Folder"), [&]() {
                return scenefolders::create(scene, path + QLatin1Char('/') + leaf);
            });
        });

        menu.addSeparator();
        QAction *del = menu.addAction(tr("Delete Folder"));
        del->setToolTip(tr("Deletes the folder only — its contents move up a level."));
        connect(del, &QAction::triggered, this, [this, path]() {
            runFolderEdit(tr("Remove Folder"), [&]() {
                return scenefolders::remove(scene, path);
            });
        });

        menu.exec(ui->sceneTree->mapToGlobal(pos));
        return;
    }

    auto nodeId = item->data(0, Qt::UserRole).toLongLong();
    auto node = nodeList.value(nodeId);
    if (!node) return;

	selectedNode = node;

    // The menu acts on the SELECTION when the right-clicked row is part of it,
    // and on the clicked row alone otherwise — the behaviour every file manager
    // has, and the reason right-clicking one of five selected objects can file
    // all five.
    QList<iris::SceneNodePtr> targets = selectedNodes();
    if (!targets.contains(node)) targets = { node };

    QMenu menu;
	menu.setStyleSheet(StyleSheet::QMenuDarkPadded());

    QAction* action;

    // FIRST RESIDENT: Move to ▸ (owner, 2026-09-06).
    buildMoveToMenu(&menu, targets);
    menu.addSeparator();

    action = new QAction(QIcon(), "Rename", this);
    connect(action, &QAction::triggered, this, [&]() { ui->sceneTree->editItem(item); });
    menu.addAction(action);

    // The world node isn't removable
    if (node->isRemovable()) {
        action = new QAction(QIcon(), "Delete", this);
        connect(action, SIGNAL(triggered()), this, SLOT(deleteNode()));
        menu.addAction(action);
    }

    if (node->isDuplicable()) {
        action = new QAction(QIcon(), "Duplicate", this);
        connect(action, SIGNAL(triggered()), this, SLOT(duplicateNode()));
        menu.addAction(action);
    }

    // ---- CUT / COPY / PASTE (CLIPBOARD_SPEC §3.1) --------------------------
    //
    // The tree had no clipboard rows at all: Ctrl+C/Ctrl+V were the only way to
    // reach the capability, which is not how anyone looks for it. These call the
    // same ClipboardService verbs the chords do — never a second implementation
    // — and act on the SAME target set the rest of this menu does (the
    // right-clicked row, or the whole selection when the row is part of it).
    if (ClipboardService *clip = mainWindow && mainWindow->studioServices()
                                     ? mainWindow->studioServices()->clipboard : nullptr) {
        menu.addSeparator();
        if (!node->isRootNode()) {
            action = new QAction(QIcon(), tr("Copy"), this);
            connect(action, &QAction::triggered, this, [clip, targets]() {
                clip->copyNodes(targets);
            });
            menu.addAction(action);

            if (node->isRemovable()) {
                action = new QAction(QIcon(), tr("Cut"), this);
                connect(action, &QAction::triggered, this, [clip, targets]() {
                    clip->cutNodes(targets);
                });
                menu.addAction(action);
            }
        }
        // Offered only when there is something a tree paste can land — a menu
        // row that always exists and usually refuses teaches nothing.
        const auto payload = clip->contents();
        if (!payload.itemsOfKind(QLatin1String(clipboardformat::kind::node())).isEmpty()) {
            action = new QAction(QIcon(), tr("Paste"), this);
            connect(action, &QAction::triggered, this, [this, clip, node]() {
                // BESIDE THE CLICKED ROW, which is what a paste in a tree means
                // (the chord's D7 placement is beside the PRIMARY; here the
                // right-clicked row is the anchor the user pointed at).
                ClipboardPasteOptions options;
                if (auto parent = node->getParent()) {
                    options.parentGuid = parent->getGUID();
                    const int after = node->siblingIndex();
                    options.index = after >= 0 ? after + 1 : -1;
                } else {
                    options.parentGuid = node->getGUID();   // the World root itself
                }
                const auto result = clip->paste(options);
                if (!result.missing.isEmpty() && mainWindow)
                    QMessageBox::warning(this, tr("Paste"),
                        tr("%1 asset(s) the copied objects need are not in this library.")
                            .arg(result.missing.size()));
            });
            menu.addAction(action);
        }
    }

	action = new QAction(QIcon(), "Focus Camera", this);
	connect(action, SIGNAL(triggered()), this, SLOT(focusOnNode()));
	menu.addAction(action);

	// "Make Reflective" (PLANAR_REFLECTIONS_SPEC.md §7). The Properties row is
	// the discoverable home for the flag; this is the one-click path for the
	// case that actually happens — the user has just selected a floor. Same
	// service call, so a mesh that cannot be a plane is refused and put back
	// here exactly as it is there.
	if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
		const bool isReflector = node->getPlanarReflector();
		action = new QAction(QIcon(), isReflector ? "Stop Reflecting" : "Make Reflective", this);
		connect(action, &QAction::triggered, this, [this, node, isReflector]() {
			QString error;
			IEditorViewport *vp = mainWindow ? mainWindow->viewport() : nullptr;
			if (!planarreflectors::set(node, !isReflector, vp, &error))
				QMessageBox::warning(this, tr("Not a reflection plane"), error);
		});
		menu.addAction(action);
	}

    if (node->isPhysicsBody) {
        QMenu *physicsMenu = menu.addMenu("Physics");
        QMenu *addConstraintsMenu = physicsMenu->addMenu("Add Constraint");
        QAction *p2pConstraint = addConstraintsMenu->addAction("Ball Constraint");
        QAction *dof6Constraint = addConstraintsMenu->addAction("6Dof Constraint");

        box = new QComboBox;

        auto rootNode = scene->getRootNode();

        connect(p2pConstraint, &QAction::triggered, this, [&]() {
            box->addItem("null", "");

            for (auto childNode : rootNode->children()) {
                if (childNode->isPhysicsBody && childNode->getGUID() != node->getGUID()) {
                    box->addItem(childNode->getName(), childNode->getGUID());
                }
            }

            connect<void(QComboBox::*)(int)>(box, &QComboBox::currentIndexChanged, this, [&](int index) {
                constraintsPicked(index, iris::PhysicsConstraintType::Ball);
            });

            QDialog d;
            auto dl = new QVBoxLayout();
            dl->addWidget(box);
            d.setLayout(dl);
            d.exec();
        });

        connect(dof6Constraint, &QAction::triggered, this, [&]() {
            box->addItem("null", "");

            for (auto childNode : rootNode->children()) {
                if (childNode->isPhysicsBody && childNode->getGUID() != node->getGUID()) {
                    box->addItem(childNode->getName(), childNode->getGUID());
                }
            }

            connect<void(QComboBox::*)(int)>(box, &QComboBox::currentIndexChanged, this, [&](int index) {
                constraintsPicked(index, iris::PhysicsConstraintType::Dof6);
            });

            QDialog d;
            auto dl = new QVBoxLayout();
            dl->addWidget(box);
            d.setLayout(dl);
            d.exec();
        });
    }

	if (node->isExportable()) {
		QMenu *subMenu = menu.addMenu("Export");

		if (node->getSceneNodeType() == iris::SceneNodeType::Mesh ||
            node->getSceneNodeType() == iris::SceneNodeType::Empty)
        {
            if (!node->isBuiltIn) {
                QAction *exportAsset = subMenu->addAction("Export Object");
                connect(exportAsset, &QAction::triggered, this, [this, node]() {
                    mainWindow->exportNode(node, ModelTypes::Object);
                });
            }

			QAction *exportMat = subMenu->addAction("Create Material");
			connect(exportMat, &QAction::triggered, this, [this, node]() {
				createMaterial();
			});
		}
		else if (node->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {
			QAction *exportPSystem = subMenu->addAction("Export Particle System");
			connect(exportPSystem, &QAction::triggered, this, [this, node]() {
                mainWindow->exportNode(node, ModelTypes::ParticleSystem);
			});
		}
	}

	// attchment
	auto nodeScene = node->getScene();
	if (nodeScene && nodeScene->getRootNode() != node) {
		QMenu *attachMenu = menu.addMenu("Attach..");

		QAction* attachChildrenMenu = attachMenu->addAction("Attach All Children");
		connect(attachChildrenMenu, &QAction::triggered, this, [this, node]() {
			this->attachAllChildren(node);
		});

		QAction* detachChildrenMenu = attachMenu->addAction("Detach From Parent");
		connect(detachChildrenMenu, &QAction::triggered, this, [this, node]() {
			this->detachFromParent(node);
		});
	}

    menu.exec(ui->sceneTree->mapToGlobal(pos));
}

void SceneHierarchyWidget::constraintsPicked(int constraintGuidToIndex, iris::PhysicsConstraintType type)
{
	iris::ConstraintProperty constraintProp;
	constraintProp.constraintFrom = selectedNode->getGUID();
	constraintProp.constraintTo = box->itemData(constraintGuidToIndex).toString();
	constraintProp.constraintType = type;

	selectedNode->physicsProperty.constraints.append(constraintProp);
}

// The menu rows and the toolbar button act on the SELECTION SET
// (EDITOR_MULTISELECT_SPEC §2.5). MainWindow::deleteNode/duplicateNode are
// themselves set-aware now, so these stay one-line delegators and all three
// entry points (menu, toolbar, Del/Ctrl+D) share one implementation and one
// undo step.
void SceneHierarchyWidget::deleteNode()
{
    mainWindow->deleteNode();
    selectedNode.clear();
}

void SceneHierarchyWidget::duplicateNode()
{
    mainWindow->duplicateNode();
}

void SceneHierarchyWidget::focusOnNode()
{
	// focusOnSelection frames the whole set's union bounds (§2.3); the
	// single-node call is the fallback for a viewport that has no set.
	if (mainWindow && mainWindow->viewport()) {
		if (mainWindow->studioServices() && mainWindow->studioServices()->selection &&
			mainWindow->studioServices()->selection->count() > 1)
			mainWindow->viewport()->focusOnSelection();
		else
			mainWindow->viewport()->focusOnNode(selectedNode);
	}
}

void SceneHierarchyWidget::exportNode(const iris::SceneNodePtr &node, ModelTypes modelType)
{
	mainWindow->exportNode(node, modelType);
}

void SceneHierarchyWidget::createMaterial()
{
	mainWindow->createMaterial();
}

void SceneHierarchyWidget::exportParticleSystem(const iris::SceneNodePtr &node)
{
	mainWindow->exportNode(node, ModelTypes::ParticleSystem);
}

void SceneHierarchyWidget::attachAllChildren()
{
	attachAllChildren(selectedNode);
}

void SceneHierarchyWidget::detachFromParent()
{
	detachFromParent(selectedNode);
}

void SceneHierarchyWidget::showHideNode(QTreeWidgetItem* item, bool show)
{
	if (isFolderItem(item)) {
		for (int i = 0; i < item->childCount(); i++) showHideNode(item->child(i), show);
		return;
	}
	qint64 nodeId = item->data(1,Qt::UserRole).toLongLong();
    auto node = nodeList[nodeId];

    if (show) {
        node->show();
    } else {
        node->hide();
    }

	for (int i = 0; i < item->childCount(); i++) {
		showHideNode(item->child(i), show);
	}
}

void SceneHierarchyWidget::repopulateTree()
{
    if (!scene) return;
    auto rootNode = scene->getRootNode();
    if (!rootNode) return;

    // Remember which folders the user had closed: a folder tree that springs
    // fully open on every edit is unusable, and this function runs on every
    // add, delete, reparent and folder gesture.
    QStringList wereCollapsed = collapsedFolders;
    for (auto it = folderItemList.constBegin(); it != folderItemList.constEnd(); ++it) {
        if (it.value() && !it.value()->isExpanded()) {
            if (!wereCollapsed.contains(it.key())) wereCollapsed.append(it.key());
        } else {
            wereCollapsed.removeAll(it.key());
        }
    }
    collapsedFolders = wereCollapsed;

    auto rootTreeItem = new QTreeWidgetItem();

	QIcon *hiddenIcon = new QIcon;
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-globe-64.png"), QIcon::Normal);
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-globe-64.png"), QIcon::Selected);

    rootTreeItem->setText(0, rootNode->getName());
    rootTreeItem->setData(0, Qt::UserRole, QVariant::fromValue(rootNode->getNodeId()));
	rootTreeItem->setIcon(0, *hiddenIcon);

    // populate tree
    nodeList.clear();
    treeItemList.clear();
    folderItemList.clear();

    nodeList.insert(rootNode->getNodeId(), rootNode);
    treeItemList.insert(rootNode->getNodeId(), rootTreeItem);

    // FOLDER ROWS FIRST, so that every folder exists (including the empty ones
    // that only the scene's explicit list knows about) before anything is filed
    // into it, and so folders sort above the loose objects at each level.
    for (const QString &path : scenefolders::all(scene)) folderItemFor(path);

    populateTree(rootTreeItem, rootNode);

    // THE SELECTION SURVIVES THE REBUILD (EDITOR_MULTISELECT_SPEC §2.2).
    // clear() below throws away every row, and nothing used to put the
    // selection back — so a Shift-range followed by any folder edit, reparent
    // or hierarchyChanged lost the set the moment the command finished. The
    // snapshot is by nodeId, which is exactly what structural undo preserves
    // and what treeItemList is keyed on.
    QList<qint64> selectedIds;
    for (const auto &n : selectedNodes()) selectedIds.append(n->getNodeId());
    const qint64 primaryId = selectedNode ? selectedNode->getNodeId() : -1;

    ui->sceneTree->clear();
    ui->sceneTree->addTopLevelItem(rootTreeItem);
    ui->sceneTree->expandItem(rootTreeItem);
    for (auto it = folderItemList.constBegin(); it != folderItemList.constEnd(); ++it)
        if (it.value()) it.value()->setExpanded(!collapsedFolders.contains(it.key()));
    //ui->sceneTree->expandAll();

    if (!selectedIds.isEmpty()) {
        QList<iris::SceneNodePtr> restored;
        if (primaryId >= 0) {
            const auto primary = nodeList.value(primaryId);
            if (!!primary) restored.append(primary);
        }
        for (qint64 id : selectedIds) {
            if (id == primaryId) continue;
            const auto node = nodeList.value(id);
            if (!!node) restored.append(node);
        }
        // Under the guard, and WITHOUT announcing: the document did not change
        // its selection, the widget just rebuilt the rows that show it.
        if (!restored.isEmpty()) setSelectedSet(restored);
    }
}

/// ONE ASSET, ONE ROW (owner rule, 2026-09-11). An import's parts are marked
/// `attached` (MeshNode::buildNodeFromModel: the root is NOT, everything under
/// it is), and those parts are the asset's insides — a Mixamo character is a
/// Sketchfab wrapper, a RootNode, a Head mesh and five material splits, i.e.
/// NINE rows for one thing the user dropped. The outliner shows the asset
/// root; the parts stay in the document, where scripts, the mirror and the
/// Avatar module read them.
///
/// The rows are NOT CREATED rather than hidden, deliberately: the shift-range
/// selection walks the widget's rows (EDITOR_MULTISELECT_SPEC §2.2), so a
/// hidden row would still be a selectable member of a range.
///
/// "Attach All Children" / "Detach From Parent" in the row's context menu are
/// what fold and unfold a subtree by hand.
bool SceneHierarchyWidget::isAssetPart(const iris::SceneNodePtr &node) const
{
    if (!node || !scene) return false;
    auto parent = node->getParent();
    return node->isAttached() && parent && parent != scene->getRootNode();
}

void SceneHierarchyWidget::populateTree(QTreeWidgetItem* parentTreeItem,
                                        QSharedPointer<iris::SceneNode> sceneNode)
{
    // Folders group the ROOT LEVEL only (§6b): a child of the world root is
    // shown under its folder row, everything deeper is shown under its parent
    // exactly as it always was. The two trees are orthogonal, so this is the
    // ONE place folders touch the layout.
    const bool rootLevel = scene && sceneNode == scene->getRootNode();

    const int kids = sceneNode->childCount();
    for (int i = 0; i < kids; ++i) {
        iris::SceneNode *raw = sceneNode->childAt(i);
        if (!raw) continue;
        const iris::SceneNodePtr childNode = raw->sharedFromThis();
        // The asset's own parts are not scene rows (see isAssetPart): skip the
        // row AND the subtree under it.
        if (isAssetPart(childNode)) { nodeList.insert(childNode->getNodeId(), childNode); continue; }
        auto childTreeItem = createTreeItems(childNode);
        QTreeWidgetItem *host = parentTreeItem;
        if (rootLevel) {
            const QString folder = scenefolders::normalize(childNode->folderPath);
            if (!folder.isEmpty())
                if (auto *folderRow = folderItemFor(folder)) host = folderRow;
        }
        host->addChild(childTreeItem);
        nodeList.insert(childNode->getNodeId(), childNode);
        treeItemList.insert(childNode->getNodeId(), childTreeItem);
        populateTree(childTreeItem, childNode);
    }
}

QTreeWidgetItem *SceneHierarchyWidget::createTreeItems(iris::SceneNodePtr node)
{
    auto childTreeItem = new QTreeWidgetItem();
    childTreeItem->setFlags(childTreeItem->flags() | Qt::ItemIsEditable);
    childTreeItem->setText(0, node->getName());
    childTreeItem->setData(0, Qt::UserRole, QVariant::fromValue(node->getNodeId()));
	childTreeItem->setData(1, Qt::UserRole, QVariant::fromValue(node->isVisible()));
	childTreeItem->setData(2, Qt::UserRole, QVariant::fromValue(node->isPickable()));

	QIcon *nodeIcon = new QIcon;
	
	if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-mesh-32.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-mesh-32.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Light) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-sun-48.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-sun-48.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-snow-storm-26.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-snow-storm-26.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Empty) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-average-math-filled-50.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-average-math-filled-50.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Decal) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-picture-50.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-picture-50.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Camera) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-camera-48.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-camera-48.png"), QIcon::Selected);
	}

	childTreeItem->setIcon(0, *nodeIcon);
	
	node->isVisible() ? childTreeItem->setIcon(1, *visibleIcon) : childTreeItem->setIcon(1, *hiddenIcon);
	node->isPickable() ? childTreeItem->setIcon(2, *pickableIcon) : childTreeItem->setIcon(2, *disabledIcon);

    return childTreeItem;
}

void SceneHierarchyWidget::hideItemAndChildren(QTreeWidgetItem * item)
{
	// A folder row carries no node — recurse THROUGH it, never into
	// nodeList[0] (a null shared pointer waiting to be dereferenced).
	if (isFolderItem(item)) {
		for (int i = 0; i < item->childCount(); i++) hideItemAndChildren(item->child(i));
		return;
	}
	qint64 nodeId = item->data(0, Qt::UserRole).toLongLong();
	item->setIcon(1, *hiddenIcon);
	nodeList[nodeId]->hide();
	item->setData(1, Qt::UserRole, QVariant::fromValue(false));

	for (int i = 0; i < item->childCount(); i++) {
		hideItemAndChildren(item->child(i));
	}
}

void SceneHierarchyWidget::showItemAndChildren(QTreeWidgetItem * item)
{
	// A folder row carries no node — recurse THROUGH it, never into
	// nodeList[0] (a null shared pointer waiting to be dereferenced).
	if (isFolderItem(item)) {
		for (int i = 0; i < item->childCount(); i++) showItemAndChildren(item->child(i));
		return;
	}
	qint64 nodeId = item->data(0, Qt::UserRole).toLongLong();
	item->setIcon(1, *visibleIcon);
	nodeList[nodeId]->show();
	item->setData(1, Qt::UserRole, QVariant::fromValue(true));

	for (int i = 0; i < item->childCount(); i++) {
		showItemAndChildren(item->child(i));
	}
}

//todo : attach physics objects
void SceneHierarchyWidget::attachAllChildren(iris::SceneNodePtr node)
{
	_attachAllChildren(node);
	// Attachment decides which rows EXIST now (isAssetPart), so folding a
	// subtree into its root is a repopulate, not a recolour. (The old
	// refreshAttachmentColors painted every row the same white in both
	// branches of its own condition — it was a no-op and is deleted.)
	repopulateTree();
}

void SceneHierarchyWidget::_attachAllChildren(iris::SceneNodePtr node)
{
	const int kids = node->childCount();
	for (int i = 0; i < kids; ++i) {
		iris::SceneNode *child = node->childAt(i);
		if (!child) continue;
		child->setAttached(true);
		attachAllChildren(child->sharedFromThis());
	}
}


void SceneHierarchyWidget::detachFromParent(iris::SceneNodePtr node)
{
	node->setAttached(false);
	repopulateTree();
}

void SceneHierarchyWidget::lockItemAndChildren(QTreeWidgetItem *item)
{
    // A folder row carries no node — recurse THROUGH it, never into
    // nodeList[0] (a null shared pointer waiting to be dereferenced).
    if (isFolderItem(item)) {
        for (int i = 0; i < item->childCount(); i++) lockItemAndChildren(item->child(i));
        return;
    }
    qint64 nodeId = item->data(0, Qt::UserRole).toLongLong();
    item->setIcon(2, *disabledIcon);
    nodeList[nodeId]->setPickable(false);
    item->setData(2, Qt::UserRole, QVariant::fromValue(false));

    for (int i = 0; i < item->childCount(); i++) {
        lockItemAndChildren(item->child(i));
    }
}

void SceneHierarchyWidget::releaseItemAndChildren(QTreeWidgetItem *item)
{
    // A folder row carries no node — recurse THROUGH it, never into
    // nodeList[0] (a null shared pointer waiting to be dereferenced).
    if (isFolderItem(item)) {
        for (int i = 0; i < item->childCount(); i++) releaseItemAndChildren(item->child(i));
        return;
    }
    qint64 nodeId = item->data(0, Qt::UserRole).toLongLong();
    item->setIcon(2, *pickableIcon);
    nodeList[nodeId]->setPickable(true);
    item->setData(2, Qt::UserRole, QVariant::fromValue(true));

    for (int i = 0; i < item->childCount(); i++) {
        releaseItemAndChildren(item->child(i));
    }
}

void SceneHierarchyWidget::insertChild(iris::SceneNodePtr childNode)
{
    auto parentNode = childNode->getParent();
    if (!parentNode) return;
    auto parentTreeItem = treeItemList.value(parentNode->nodeId);
    if (!parentTreeItem) return;
    // A root-level node that carries a folder belongs under the folder ROW, not
    // under the world row (§6b) — this is the incremental twin of populateTree.
    if (scene && parentNode == scene->getRootNode()) {
        const QString folder = scenefolders::normalize(childNode->folderPath);
        if (!folder.isEmpty())
            if (auto *folderRow = folderItemFor(folder)) parentTreeItem = folderRow;
    }
    if (isAssetPart(childNode)) {
        // The incremental twin of populateTree's skip: an asset's parts arrive
        // with it (AddSceneNodeCommand walks the subtree) and get no rows.
        nodeList.insert(childNode->getNodeId(), childNode);
        for (auto child : childNode->children()) insertChild(child);
        return;
    }
    auto childItem = createTreeItems(childNode);
    // Folder rows share a level with the objects, and they come first — so a
    // DOCUMENT sibling index is not a ROW index at the root level any more.
    // Skip past the folder rows and clamp: with no folders this is exactly the
    // old insertChild(siblingIndex), and with folders a new object lands after
    // them rather than above them.
    int firstObjectRow = 0;
    while (firstObjectRow < parentTreeItem->childCount() &&
           isFolderItem(parentTreeItem->child(firstObjectRow)))
        ++firstObjectRow;
    const int at = qBound(firstObjectRow, firstObjectRow + childNode->siblingIndex(),
                          parentTreeItem->childCount());
    parentTreeItem->insertChild(at, childItem);

    // add to lists
    nodeList.insert(childNode->getNodeId(), childNode);
    treeItemList.insert(childNode->getNodeId(), childItem);

    // recursively add children
    for (auto child: childNode->children()) insertChild(child);
}

void SceneHierarchyWidget::removeChild(iris::SceneNodePtr childNode)
{
    // remove from heirarchy
    auto nodeTreeItem = treeItemList.value(childNode->nodeId);
    if (!nodeTreeItem || !nodeTreeItem->parent()) return;
    nodeTreeItem->parent()->removeChild(nodeTreeItem);

    // remove from lists
    nodeList.remove(childNode->getNodeId());
    treeItemList.remove(childNode->getNodeId());
}

void SceneHierarchyWidget::OnLstItemsCommitData(QWidget *listItem)
{
    auto *editor = qobject_cast<QLineEdit*>(listItem);
    if (!editor) return;
    const QString newName = editor->text().trimmed();
    if (newName.isEmpty()) return;

    // Inline rename serves BOTH row kinds now. The edited row is the current
    // one (Qt makes it current to open the editor), which is also the only way
    // to tell a folder rename from a node rename — commitData carries the
    // editor widget, not the item.
    auto *item = ui->sceneTree->currentItem();
    if (isFolderItem(item)) {
        const QString path = folderPathOf(item);
        const QString leaf = scenefolders::normalize(newName);
        // Put the row's text back to the stored leaf: the rename below rebuilds
        // the tree from the document, so whatever the editor left behind on
        // this (about to be deleted) item is irrelevant — except when the
        // rename is REFUSED, where the row must not keep the rejected name.
        item->setText(0, scenefolders::leafOf(path));
        if (leaf.isEmpty() || leaf.contains(QLatin1Char('/'))) return;
        runFolderEdit(tr("Rename Folder"), [&]() {
            return scenefolders::rename(scene, path, leaf);
        });
        return;
    }

    // A NODE ROW: the same edit `node.rename` makes (API-first) — sibling-unique
    // by the copy rule, one undo step. It used to be a bare setName, which
    // recorded nothing (Ctrl+Z could not take a typo back) and happily made two
    // siblings called "Cube". The node is the one the ROW names, not whatever
    // happens to be selected: the two agree when the editor was opened by a
    // double-click, but the context menu's Rename does not select.
    const qint64 nodeId = item ? item->data(0, Qt::UserRole).toLongLong() : 0;
    iris::SceneNodePtr node = nodeList.value(nodeId);
    if (!node) node = selectedNode;
    if (!node) return;
    SceneEditService *service = (mainWindow && mainWindow->studioServices())
                                    ? mainWindow->studioServices()->sceneEdit : nullptr;
    const QString given = service ? service->renameNode(node, newName) : QString();
    // The service rebuilds the tree on success; on a refusal (or no service)
    // put the row back to the name the document actually holds.
    if (given.isEmpty() && item && treeItemList.value(nodeId) == item)
        item->setText(0, node->getName());
}

QTreeWidget * SceneHierarchyWidget::getWidget()
{
    return ui->sceneTree;
}

void SceneHierarchyWidget::selectNode(QString nodeId)
{
	std::function<void(QTreeWidgetItem*, QString)> hightlightNode;
	hightlightNode = [=](QTreeWidgetItem* treeItem, QString nodeId)
	{
		for (int i = 0; i < treeItem->childCount(); i++) {
			auto item = treeItem->child(i);
			if (item->data(0, Qt::UserRole) == nodeId) {
                ui->sceneTree->setCurrentItem(item);
				return;
			}

			hightlightNode(item, nodeId);
		}
	};
}

SceneHierarchyWidget::~SceneHierarchyWidget()
{
    delete ui;
}
